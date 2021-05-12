//
//  ViewController.m
//  fishhook_demo
//
//  Created by hapii on 2021/5/8.
//

#import "ViewController.h"
#import "fishhook.h"
#import <objc/runtime.h>
#import <MapKit/MapKit.h>
#import "LocationHelper.h"
#import <dlfcn.h>
#include <dispatch/dispatch.h>
//#import "fishhook.c"
#import "ObjcHook.h"


@interface ViewController ()

@property (nonatomic, strong) LocationHelper * locationHelper;
@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    _locationHelper = [[LocationHelper alloc]initSuperController:self];
    
    NSLog(@"hook前");
    struct rebinding nslog;
    
    nslog.name = "NSLog";
    nslog.replacement = new_nslog;
    nslog.replaced = (void *)&ori_nslog;
    
    struct rebinding rebinds[1] = {nslog};
    
    rebind_symbols(rebinds, 1);
    
}

static void (*ori_nslog)(NSString * format, ...);

void new_nslog(NSString * format, ...) {
    //自定义的替换函数
    format = [format stringByAppendingFormat:@" Gua "];
    ori_nslog(format);
}

- (void)hahha {
    
    NSLog(@"呃呃呃呃呃");
    dispatch_after(2, dispatch_get_main_queue(), ^{
        NSLog(@"nenenen");
    });
}

- (void)viewWillAppear:(BOOL)animated {
    
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [self cs];
    });
}

- (void)cs {
    
    smCallTraceStart();
    NSLog(@"daliya~~");
    [self hahha];
    [_locationHelper determineLocation];
    //[[TimeProfiler shareInstance] TPStopTrace];
    smCallTraceStop();
}





@end

