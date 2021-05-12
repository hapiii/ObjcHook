//
//  ObjcHook.c
//  fishhook_demo
//
//  Created by hapii on 2021/5/11.
//


#include "ObjcHook.h"

#ifdef __aarch64__


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include "fishhook.h"


static bool _call_record_enabled = true;
///执行时间最低额度
static uint64_t _min_time_cost = 1000;
///方法树最大深度
static int _max_call_depth = 3;

static pthread_key_t _thread_key;

__unused static id (*orig_objc_msgSend)(id, SEL, ...);

static smCallRecord *_smCallRecords;
static int _smRecordNum;
static int _smRecordAlloc;

///method数据基础模型
typedef struct {
    id self; //通过 object_getClass 能够得到 Class 再通过 NSStringFromClass 能够得到类名
    Class cls;
    SEL cmd; //通过 NSStringFromSelector 方法能够得到方法名
    uint64_t time; //us
    uintptr_t lr; // link register
} thread_call_record;

///
typedef struct {
    thread_call_record *stack;//指向数组的指针
    int allocated_length;
    int index; //index 记录当前调用方法树的深度
    bool is_main_thread;//是否主线程
} thread_call_stack;

static inline thread_call_stack * get_thread_call_stack() {
    ///指定线程获取
    thread_call_stack *cs = (thread_call_stack *)pthread_getspecific(_thread_key);
    if (cs == NULL) {
        cs = (thread_call_stack *)malloc(sizeof(thread_call_stack));
        cs->stack = (thread_call_record *)calloc(128, sizeof(thread_call_record));
        cs->allocated_length = 64;
        cs->index = -1;
        //是否是主线程
        cs->is_main_thread = pthread_main_np();
        ///设置到指定线程
        pthread_setspecific(_thread_key, cs);
    }
    return cs;
}

static void release_thread_call_stack(void *ptr) {
    thread_call_stack *cs = (thread_call_stack *)ptr;
    if (!cs) return;
    if (cs->stack) free(cs->stack);
    free(cs);
}

static inline void push_call_record(id _self, Class _cls, SEL _cmd, uintptr_t lr) {
    thread_call_stack *cs = get_thread_call_stack();
    if (cs) {
        //增加深度
        int nextIndex = (++cs->index);
        if (nextIndex >= cs->allocated_length) {
            cs->allocated_length += 64;
            ///扩容
            cs->stack = (thread_call_record *)realloc(cs->stack, cs->allocated_length * sizeof(thread_call_record));
        }
        thread_call_record *newRecord = &cs->stack[nextIndex];
        newRecord->self = _self;
        newRecord->cls = _cls;
        newRecord->cmd = _cmd;
        newRecord->lr = lr;
        if (cs->is_main_thread && _call_record_enabled) {
            struct timeval now;
            gettimeofday(&now, NULL);
            newRecord->time = (now.tv_sec % 100) * 1000000 + now.tv_usec;
        }
        
        printf("push_call_record操作 class:%s method：%s \n\n", object_getClassName(_cls),  sel_getName(_cmd));
    }
    
}

static inline uintptr_t pop_call_record() {
    thread_call_stack *cs = get_thread_call_stack();
    int curIndex = cs->index;
    int nextIndex = cs->index--;
    thread_call_record *pRecord = &cs->stack[nextIndex];
    
    if (cs->is_main_thread && _call_record_enabled) {
        struct timeval now;
        gettimeofday(&now, NULL);
        uint64_t time = (now.tv_sec % 100) * 1000000 + now.tv_usec;
        if (time < pRecord->time) {
            time += 100 * 1000000;
        }
        uint64_t cost = time - pRecord->time;
        if (cost > _min_time_cost && cs->index < _max_call_depth) {
            if (!_smCallRecords) {
                _smRecordAlloc = 1024;
                _smCallRecords = malloc(sizeof(smCallRecord) * _smRecordAlloc);
            }
            _smRecordNum++;
            if (_smRecordNum >= _smRecordAlloc) {
                _smRecordAlloc += 1024;
                _smCallRecords = realloc(_smCallRecords, sizeof(smCallRecord) * _smRecordAlloc);
            }
            smCallRecord *log = &_smCallRecords[_smRecordNum - 1];
            log->cls = pRecord->cls;
            log->depth = curIndex;
            log->sel = pRecord->cmd;
            log->time = cost;
            
        }
       
       
    }
    
    printf("截住后: \n");
    return pRecord->lr;
}

void before_objc_msgSend(id self, SEL _cmd, uintptr_t lr) {
    push_call_record(self, object_getClass(self), _cmd, lr);
    
}

uintptr_t after_objc_msgSend() {
    return pop_call_record();
    
}

///方法调用
#define call(b, value) \
__asm volatile ("stp x8, x9, [sp, #-16]!\n"); \
__asm volatile ("mov x12, %0\n" :: "r"(value)); \
__asm volatile ("ldp x8, x9, [sp], #16\n"); \
__asm volatile (#b " x12\n");

//保存上下文
#define save() \
__asm volatile ( \
"stp x8, x9, [sp, #-16]!\n" \
"stp x6, x7, [sp, #-16]!\n" \
"stp x4, x5, [sp, #-16]!\n" \
"stp x2, x3, [sp, #-16]!\n" \
"stp x0, x1, [sp, #-16]!\n");

//恢复上下文
#define load() \
__asm volatile ( \
"ldp x0, x1, [sp], #16\n" \
"ldp x2, x3, [sp], #16\n" \
"ldp x4, x5, [sp], #16\n" \
"ldp x6, x7, [sp], #16\n" \
"ldp x8, x9, [sp], #16\n" );


#define link(b, value) \
__asm volatile ("stp x8, lr, [sp, #-16]!\n"); \
__asm volatile ("sub sp, sp, #16\n"); \
call(b, value); \
__asm volatile ("add sp, sp, #16\n"); \
__asm volatile ("ldp x8, lr, [sp], #16\n");

#define ret() __asm volatile ("ret\n");

__attribute__((__naked__))

static void hook_Objc_msgSend() {
    
    //保存上下文
    save()
    
    __asm volatile ("mov x2, lr\n");
    __asm volatile ("mov x3, x4\n");
    

    call(blr, &before_objc_msgSend)
    
    // 恢复
    load()
    // 调用原始的objc_msgSend
    call(blr, orig_objc_msgSend)
    save()
    
    // Call our after_objc_msgSend.
    call(blr, &after_objc_msgSend)
    // restore lr
    __asm volatile ("mov lr, x0\n");
    load()
    
    
    // return
    ret()
     
}


#pragma mark public

void smCallTraceStart() {
    _call_record_enabled = true;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        pthread_key_create(&_thread_key, &release_thread_call_stack);
        rebind_symbols((struct rebinding[6]){
            {"objc_msgSend", (void *)hook_Objc_msgSend, (void **)&orig_objc_msgSend},
        }, 1);
    });
}

void smCallTraceStop() {
    _call_record_enabled = false;
}

void smCallConfigMinTime(uint64_t us) {
    _min_time_cost = us;
}

void smCallConfigMaxDepth(int depth) {
    _max_call_depth = depth;
}

smCallRecord *smGetCallRecords(int *num) {
    if (num) {
        *num = _smRecordNum;
    }
    return _smCallRecords;
}

void smClearCallRecords() {
    if (_smCallRecords) {
        free(_smCallRecords);
        _smCallRecords = NULL;
    }
    _smRecordNum = 0;
}

#else
///非X64空实现
void smCallTraceStart() {}
void smCallTraceStop() {}
void smCallConfigMinTime(uint64_t us) {
}
void smCallConfigMaxDepth(int depth) {
}

smCallRecord *smGetCallRecords(int *num) {
    if (num) {
        *num = 0;
    }
    return NULL;
}
void smClearCallRecords() {}

#endif
