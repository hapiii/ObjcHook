# ObjcHook
fishhook  hook
#### fishhook介绍

我们都知道OC的Runtime里的Method Swizzle可以动态改变SEL（方法编号）和IMP（方法实现）的对应关系，实现OC方法的hook。


而[fishhook](https://github.com/facebook/fishhook)是实现hook C函数的一个框架。

dyld 是英文 the dynamic link editor 的简写，动态链接器，是苹果操作系统的一个重要的组成部分

系统内核在加载 Mach-O 文件时，都需要用 dyld（位于 /usr/lib/dyld ）程序进行链接

C语言是静态的，编译时就确定了函数地址。系统函数在dylb加载时候才能确定，苹果针对mach -O提供了PIC技术。即在MatchO的_Data段中添加懒加载表(Lazy Symbol Pointers)和非懒加载表(Non-Lazy Symbol Pointers)这两个表,让系统的函数在编译的时候先指向懒加载表(Lazy Symbol Pointers)或非懒加载表(Non-Lazy Symbol Pointers)中的符号地址,这两个表中的符号的地址的指向在编译的时候并没有指向任何地方,app启动,被dyld加载到内存,就进行链接, 给这2个表赋值动态缓存库的地址进行符号绑定。

 
![machOView查看地址](https://upload-images.jianshu.io/upload_images/2246303-69f75c0309fcfd70.png?imageMogr2/auto-orient/strip%7CimageView2/2/w/1240)

fishHook其实就是修改懒加载表(Lazy Symbol Pointers)、非懒加载表(Non-Lazy Symbol Pointers)中的符号地址的指向，从而达到hook的目的。
主要流程如下：

lazy symbol Point Table  -> indirect Symbols -> symbol Table -> String Table

MachO 文件有个规律，__la_symbol_ptr 节中的第 i 个数据，在 Indirect Symbols 中有对应的体现，且 index 变成了 reserved1 + I。

printf() 对应的数据，在 __la_symbol_ptr 节中是第 10 个元素，__la_symbol_ptr 中的 reserved1 为 12，那么我们找到 Indirect Symbols 中的第 22 个元素：

我们拿着 0x50，去 Symbol Table 中找到 Symbol Table 中 index = 0x50 的数据

Symbol Table 中偏移量为 0x233 的字符串，就是 _printf
![原理](https://upload-images.jianshu.io/upload_images/2246303-d819ca06a9615f32?imageMogr2/auto-orient/strip%7CimageView2/2/w/1240)

这里要注意的是，fishhook只能hook到系统C函数。因为自定义函数的实现会在mach-o的__TEXT（代码段）里。编译后，调用自定义函数的指针，是直接指向代码段中的地址的。


>  参考：
> [fishhook的实现原理浅析](https://juejin.cn/post/6844903789783154702)
#### fishhook使用方法

##### api
```
struct rebinding {
///函数名称
  const char *name;
///替换函数地址
  void *replacement;
///保存原始函数地址变量的指针
  void **replaced;
};

int rebind_symbols(struct rebinding rebindings[], size_t rebindings_nel);

int rcd_rebind_symbols_image(void *header,
                             intptr_t slide,
                             struct rcd_rebinding rebindings[],
                             size_t rebindings_nel);
```

##### 使用方法：
```
struct rebinding nsLog;
nsLog.name = "NSLog";
nsLog.replacement = nNSLog;
nsLog.replaced = (void *)&oNSLog;
struct rebinding rebinds[1] = {nsLog};

///传入结构体数组，数组大小
rebind_symbols(rebinds, 1);


static void (* oNSLog)(NSString *format, ...);
void nNSLog(NSString *format, ...) {
    oNSLog(@"%@",[format stringByAppendingString:@"被HOOK了"]);
}
```
#### fishhook 使用场景

###### 1. FBRetainCycleDetector检查内存泄漏
```objc
+ (void)hook
{
#if _INTERNAL_RCD_ENABLED
  std::lock_guard<std::mutex> l(*FB::AssociationManager::hookMutex);
  rcd_rebind_symbols((struct rcd_rebinding[2]){
    {
      "objc_setAssociatedObject",
      (void *)FB::AssociationManager::fb_objc_setAssociatedObject,
      (void **)&FB::AssociationManager::fb_orig_objc_setAssociatedObject
    },
    {
      "objc_removeAssociatedObjects",
      (void *)FB::AssociationManager::fb_objc_removeAssociatedObjects,
      (void **)&FB::AssociationManager::fb_orig_objc_removeAssociatedObjects
    }}, 2);
  FB::AssociationManager::hookTaken = true;
#endif //_INTERNAL_RCD_ENABLED
}
```
>  objc_setAssociatedObject(id _Nonnull object, const void * _Nonnull key,
                         id _Nullable value, objc_AssociationPolicy policy)

```
static void fb_objc_setAssociatedObject(id object, void *key, id value, objc_AssociationPolicy policy) {
    {
      std::lock_guard<std::mutex> l(*_associationMutex);
      // 强持有
      if (policy == OBJC_ASSOCIATION_RETAIN ||
          policy == OBJC_ASSOCIATION_RETAIN_NONATOMIC) {
        _threadUnsafeSetStrongAssociation(object, key, value);
      } else {
        // We can change the policy, we need to clear out the key
        _threadUnsafeResetAssociationAtKey(object, key);
      }
    }
///执行替换的方法
    fb_orig_objc_setAssociatedObject(object, key, value, policy);
  }
```
维护内部map
```
void _threadUnsafeSetStrongAssociation(id object, void *key, id value) {
    if (value) {
      auto i = _associationMap->find(object);
      ObjectAssociationSet *refs;
      if (i != _associationMap->end()) {
        refs = i->second;
      } else {
        refs = new ObjectAssociationSet;
        (*_associationMap)[object] = refs;
      }
///
      refs->insert(key);
    } else {
      _threadUnsafeResetAssociationAtKey(object, key);
    }
  }
```

###### 2. hook objc_msgSend，截取oc方法实现：
objc_msgSend一方面是参数不定的，而且是汇编实现。所以不能像NSLog一样简单hook,这里采用的主要思路是:

先用fish hook hook主 objc_msgSend
```
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
```

然后在自定义hook_Objc_msgSend 里执行相应的汇编代码：
```
static void hook_Objc_msgSend() {
    save()
    __asm volatile ("mov x2, lr\n");
    __asm volatile ("mov x3, x4\n");
    call(blr, &before_objc_msgSend)
    load()
    call(blr, orig_objc_msgSend)
    save()
    call(blr, &after_objc_msgSend)
    // restore lr
    __asm volatile ("mov lr, x0\n");
    load()
    ret()  
}
```
上述指令的主要操作是：

保存寄存器
调用自定义的before_objc_msgSend
恢复寄存器
调用原始objc_msgSend
再次保存寄存器
调用after_objc_msgSend
恢复寄存器
返回
```
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

```

在执行objc_msgSend前后分别执行了before_objc_msgSend和after_objc_msgSend方法

在这里因为是在主线程上，可以自定义一个模型数组在before和after里获取到同一个index并且进行操作制定index下的模型，从而计算方法用时

缺点：
1. 只能hook主线程方法
2. 只能hook oc 方法

[demo](https://github.com/hapiii/ObjcHook)

>  参考：
> 1.  [戴铭 -对 objc_msgSend 方法进行 hook 来掌握所有方法的执行耗时](https://time.geekbang.org/column/article/85331)
>  2. [字节工程师对上者的完善封装](https://juejin.cn/post/6844903875804135431)
>  3. [国外逆向原创](https://github.com/DavidGoldman/InspectiveC)


#### 可以深耕的点
1. MatchO,Dyld加载相关知识（fishhook源码实现原理）
2. arm 64 汇编学习（objc_msgSend实现代码）
