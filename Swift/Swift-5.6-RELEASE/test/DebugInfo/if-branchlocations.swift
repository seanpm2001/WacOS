// RUN: %target-swift-frontend %s -emit-sil -enable-copy-propagation=false -emit-verbose-sil -g -o - | %FileCheck %s --check-prefixes=CHECK,CHECK-NCP
// RUN: %target-swift-frontend %s -emit-sil -enable-copy-propagation -enable-lexical-borrow-scopes=false -emit-verbose-sil -g -o - | %FileCheck %s --check-prefixes=CHECK,CHECK-CP

class NSURL {}

class NSPathControlItem {
  var URL: NSURL?
}

class NSPathControl {
  var clickedPathItem: NSPathControlItem?;
}

class AppDelegate {
    
  func LogStr(_ message: String) {
  }

  func componentClicked(_ sender: AnyObject)
  {
    if let control = sender as? NSPathControl
    {
      LogStr( "Got a path control" )
      if let item = control.clickedPathItem
      {
        LogStr( "Got an NSPathControlItem" )
        // Verify that the branch's location is >= the cleanup's location.
        // (The implicit false block of the conditional
        //  below inherits the location from the condition.)
        // CHECK-CP: strong_release{{.*}}$NSPathControlItem{{.*}}line:[[@LINE+3]]
        // CHECK-CP: debug_value [poison] {{.*}}$NSPathControlItem, let, name "item"{{.*}}line:[[@LINE-7]]:14:in_prologue
        // CHECK: br{{.*}}line:[[@LINE+1]]
        if let url = item.URL
        {
          LogStr( "There is a url" )
        }
        // Verify that the branch's location is >= the cleanup's location.
        // CHECK-NCP: strong_release{{.*}}$NSPathControlItem{{.*}}line:[[@LINE+2]]
        // CHECK: br{{.*}}line:[[@LINE+1]]
      }
    }
  }
}
