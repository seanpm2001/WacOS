// RUN: %target-swift-frontend %s -emit-ir -g -o - | %FileCheck %s

class CGImageRef {}
class UIImage {
  init() {}
  var CGImage : CGImageRef? {
    get { return self.CGImage }
  }
}
class NSDictionary {}
class CIFilter {
  init (name: String) {}
}

class MyClass {
  // CHECK: define hidden %C7iuo_arg7UIImage* @_TFC7iuo_arg7MyClass11filterImagefTGSQCS_7UIImage_Sb_S1_
  func filterImage(_ image: UIImage!, _ doSomething:Bool) -> UIImage
	{
    // Test that image is in an alloca, but not an indirect location.
    // CHECK: store {{(i32|i64)}} %0, {{(i32|i64)}}* %[[ALLOCA:.*]], align
    // CHECK: call void @llvm.dbg.declare(metadata {{(i32|i64)}}* %[[ALLOCA]], metadata ![[IMAGE:.*]], metadata !{{[0-9]+}})
    // CHECK: ![[IMAGE]] = !DILocalVariable(name: "image", arg: 1
    // CHECK-NOT:                           flags:
    // CHECK-SAME:                          line: [[@LINE-7]]
    // CHECK-NOT:                           flags:
    // CHECK-SAME:                          ){{$}}
		let filter = CIFilter(name: "CIGaussianBlur")
		return image
	}
}

let a = MyClass()
let img = a.filterImage(UIImage(), true)

