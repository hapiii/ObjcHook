//
//  ObjcHook.h
//  fishhook_demo
//
//  Created by hapii on 2021/5/11.
//

#ifndef ObjcHook_h
#define ObjcHook_h

#include <stdio.h>
#include <objc/objc.h>

///记录调用方法详细信息的结构题
typedef struct {
    __unsafe_unretained Class cls;
    SEL sel;
    uint64_t time; // us (1/1000 ms)
    int depth;
} smCallRecord;

extern void smCallTraceStart(void);
extern void smCallTraceStop(void);

extern void smCallConfigMinTime(uint64_t us); //default 1000
extern void smCallConfigMaxDepth(int depth);  //default 3

extern smCallRecord *smGetCallRecords(int *num);
extern void smClearCallRecords(void);

#endif /* ObjcHook_h */
