#!/usr/bin/env bash
# The Android build of the online-play branch (report entry 53): generated code from the net trees (C:\ob\net-src),
# its own NDK tree and APK folder so main's Android build is untouched. Args as tools/ob.sh android (default: all).
source "$(dirname "$0")/net-env.sh"
export OB_ANDROID_R=C:/ob/net-android-r OB_ANDROID_APK=C:/ob/net-android-apk
bash "$(dirname "$0")/../../tools/ob.sh" android "$@"
