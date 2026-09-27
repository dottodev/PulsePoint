# The JNI entry points are resolved by name from native code, so they must survive
# shrinking even though nothing calls them from Java.
-keepclasseswithmembernames class com.pulsepoint.app.PulsePoint {
    native <methods>;
}
