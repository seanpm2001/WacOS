set(SUPPORTED_IOS_ARCHS "armv7;armv7s;arm64")
set(SUPPORTED_IOS_SIMULATOR_ARCHS "i386;x86_64")
set(SUPPORTED_TVOS_ARCHS "arm64")
set(SUPPORTED_TVOS_SIMULATOR_ARCHS "x86_64")
set(SUPPORTED_WATCHOS_ARCHS "armv7k")
set(SUPPORTED_WATCHOS_SIMULATOR_ARCHS "i386")
set(SUPPORTED_OSX_ARCHS "x86_64")

is_sdk_requested(OSX swift_build_osx)
if(swift_build_osx)
  configure_sdk_darwin(
      OSX "OS X" "${SWIFT_DARWIN_DEPLOYMENT_VERSION_OSX}"
      macosx macosx macosx "${SUPPORTED_OSX_ARCHS}")
  configure_target_variant(OSX-DA "OS X Debug+Asserts"   OSX DA "Debug+Asserts")
  configure_target_variant(OSX-RA "OS X Release+Asserts" OSX RA "Release+Asserts")
  configure_target_variant(OSX-R  "OS X Release"         OSX R  "Release")
endif()

# Compatible cross-compile SDKS for Darwin OSes: IOS, IOS_SIMULATOR, TVOS,
#   TVOS_SIMULATOR, WATCHOS, WATCHOS_SIMULATOR (archs hardcoded below).

is_sdk_requested(IOS swift_build_ios)
if(swift_build_ios)
  configure_sdk_darwin(
      IOS "iOS" "${SWIFT_DARWIN_DEPLOYMENT_VERSION_IOS}"
      iphoneos ios ios "${SUPPORTED_IOS_ARCHS}")
  configure_target_variant(IOS-DA "iOS Debug+Asserts"   IOS DA "Debug+Asserts")
  configure_target_variant(IOS-RA "iOS Release+Asserts" IOS RA "Release+Asserts")
  configure_target_variant(IOS-R  "iOS Release"         IOS R "Release")
endif()

is_sdk_requested(IOS_SIMULATOR swift_build_ios_simulator)
if(swift_build_ios_simulator)
  configure_sdk_darwin(
      IOS_SIMULATOR "iOS Simulator" "${SWIFT_DARWIN_DEPLOYMENT_VERSION_IOS}"
      iphonesimulator ios-simulator ios "${SUPPORTED_IOS_SIMULATOR_ARCHS}")
  configure_target_variant(
      IOS_SIMULATOR-DA "iOS Debug+Asserts"   IOS_SIMULATOR DA "Debug+Asserts")
  configure_target_variant(
      IOS_SIMULATOR-RA "iOS Release+Asserts" IOS_SIMULATOR RA "Release+Asserts")
  configure_target_variant(
      IOS_SIMULATOR-R  "iOS Release"         IOS_SIMULATOR R "Release")
endif()

is_sdk_requested(TVOS swift_build_tvos)
if(swift_build_tvos)
  configure_sdk_darwin(
      TVOS "tvOS" "${SWIFT_DARWIN_DEPLOYMENT_VERSION_TVOS}"
      appletvos tvos tvos "${SUPPORTED_TVOS_ARCHS}")
  configure_target_variant(TVOS-DA "tvOS Debug+Asserts"   TVOS DA "Debug+Asserts")
  configure_target_variant(TVOS-RA "tvOS Release+Asserts" TVOS RA "Release+Asserts")
  configure_target_variant(TVOS-R  "tvOS Release"         TVOS R "Release")
endif()

is_sdk_requested(TVOS_SIMULATOR swift_build_tvos_simulator)
if(swift_build_tvos_simulator)
  configure_sdk_darwin(
      TVOS_SIMULATOR "tvOS Simulator" "${SWIFT_DARWIN_DEPLOYMENT_VERSION_TVOS}"
      appletvsimulator tvos-simulator tvos "${SUPPORTED_TVOS_SIMULATOR_ARCHS}")
  configure_target_variant(
    TVOS_SIMULATOR-DA "tvOS Debug+Asserts"   TVOS_SIMULATOR DA "Debug+Asserts")
  configure_target_variant(
    TVOS_SIMULATOR-RA "tvOS Release+Asserts" TVOS_SIMULATOR RA "Release+Asserts")
  configure_target_variant(
    TVOS_SIMULATOR-R  "tvOS Release"         TVOS_SIMULATOR R "Release")
endif()

is_sdk_requested(WATCHOS swift_build_watchos)
if(swift_build_watchos)
  configure_sdk_darwin(
      WATCHOS "watchOS" "${SWIFT_DARWIN_DEPLOYMENT_VERSION_WATCHOS}"
      watchos watchos watchos "${SUPPORTED_WATCHOS_ARCHS}")
  configure_target_variant(WATCHOS-DA "watchOS Debug+Asserts"   WATCHOS DA "Debug+Asserts")
  configure_target_variant(WATCHOS-RA "watchOS Release+Asserts" WATCHOS RA "Release+Asserts")
  configure_target_variant(WATCHOS-R  "watchOS Release"         WATCHOS R "Release")
endif()

is_sdk_requested(WATCHOS_SIMULATOR swift_build_watchos_simulator)
if(swift_build_watchos_simulator)
  configure_sdk_darwin(
      WATCHOS_SIMULATOR "watchOS Simulator" "${SWIFT_DARWIN_DEPLOYMENT_VERSION_WATCHOS}"
      watchsimulator watchos-simulator watchos "${SUPPORTED_WATCHOS_SIMULATOR_ARCHS}")
  configure_target_variant(WATCHOS_SIMULATOR-DA "watchOS Debug+Asserts"   WATCHOS_SIMULATOR DA "Debug+Asserts")
  configure_target_variant(WATCHOS_SIMULATOR-RA "watchOS Release+Asserts" WATCHOS_SIMULATOR RA "Release+Asserts")
  configure_target_variant(WATCHOS_SIMULATOR-R  "watchOS Release"         WATCHOS_SIMULATOR R "Release")
endif()
