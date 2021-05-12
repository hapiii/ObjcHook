//
//  LocationHelper.m
//  fishhook_demo
//
//  Created by hapii on 2021/5/10.
//

#import "LocationHelper.h"

#import <CoreLocation/CoreLocation.h>

@interface LocationHelper()<CLLocationManagerDelegate>

@property (nonatomic, strong)CLLocationManager *localManager;

@property (nonatomic, weak)UIViewController *superController;
@end

@implementation LocationHelper

- (instancetype)initSuperController:(UIViewController *)vc {
    if (self = [super init]) {
        _superController = vc;
    }
    return  self;
}
- (CLLocationManager *)localManager {
    if (_localManager == nil) {
        _localManager = [[CLLocationManager alloc] init];
        _localManager.delegate = self;
    }
    return _localManager;
}

- (void)determineLocation {
    if ([CLLocationManager locationServicesEnabled]) {
        [self.localManager requestAlwaysAuthorization];
        [self.localManager requestWhenInUseAuthorization];
        
        //设置寻址精度
        self.localManager.desiredAccuracy = kCLLocationAccuracyBest;
        self.localManager.distanceFilter = 5.0;
        [self.localManager startUpdatingLocation];
    } else {
        [self showAlert];
    }
}

//定位失败后调用此代理方法
-(void)locationManager:(CLLocationManager *)manager didFailWithError:(NSError *)error
{
    [self showAlert];
}


- (void)showAlert {
    //设置提示提醒用户打开定位服务
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"允许定位提示" message:@"请在设置中打开定位" preferredStyle:UIAlertControllerStyleAlert];
    UIAlertAction *okAction = [UIAlertAction actionWithTitle:@"打开定位" style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
        if ([[UIDevice currentDevice].systemVersion floatValue] < 10) {
            if ([[UIApplication sharedApplication] canOpenURL:[NSURL URLWithString:@"prefs:root=LOCATION_SERVICES"]])
                [[UIApplication sharedApplication] openURL:[NSURL URLWithString:@"prefs:root=LOCATION_SERVICES"]];
        }else{
            if ([UIApplication instancesRespondToSelector:NSSelectorFromString(@"openURL:options:completionHandler:")])
                    [[UIApplication sharedApplication] openURL:[NSURL URLWithString:@"App-Prefs:root=Privacy&path=LOCATION"] options:@{} completionHandler:nil];
            }
        
    }];
    
    UIAlertAction *cancelAction = [UIAlertAction actionWithTitle:@"取消" style:UIAlertActionStyleCancel handler:nil];
    [alert addAction:okAction];
    [alert addAction:cancelAction];
    [_superController presentViewController:alert animated:YES completion:nil];
}

-(void)locationManager:(CLLocationManager *)manager didUpdateLocations:(NSArray<CLLocation *> *)locations
{
    [self.localManager stopUpdatingHeading];
    //旧址
    CLLocation *currentLocation = [locations lastObject];
    CLGeocoder *geoCoder = [[CLGeocoder alloc]init];
    //打印当前的经度与纬度
    NSLog(@"%f,%f",currentLocation.coordinate.latitude,currentLocation.coordinate.longitude);
    
    //反地理编码
    [geoCoder reverseGeocodeLocation:currentLocation completionHandler:^(NSArray<CLPlacemark *> * _Nullable placemarks, NSError * _Nullable error)
     {
        NSLog(@"反地理编码");
        NSLog(@"反地理编码%ld",placemarks.count);
        if (placemarks.count > 0) {
            CLPlacemark *placeMark = placemarks[0];
            
            /*看需求定义一个全局变量来接收赋值*/
            NSLog(@"城市----%@",placeMark.country);//当前国家
            NSLog(@"城市%@",placeMark.locality);//当前的城市
            NSLog(@"%@",placeMark.subLocality);//当前的位置
            NSLog(@"%@",placeMark.thoroughfare);//当前街道
            NSLog(@"%@",placeMark.name);//具体地址
        }
    }];
    
}
@end
