#include <android/log.h>
#include <sys/types.h>
#include <string>
#include "zygisk.hpp"

static constexpr auto TAG = "DisableMiFontOverlay";

#define LOGD(...)     __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

class DisableMiFontOverlay : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *pApi, JNIEnv *pEnv) override {
        this->api = pApi;
        this->env = pEnv;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        const char *nice_name = env->GetStringUTFChars(args->nice_name, nullptr);
        if (nice_name == nullptr) {
            LOGD("nice_name is null, skipping");
            return;
        }
        std::string pkgName(nice_name);
        env->ReleaseStringUTFChars(args->nice_name, nice_name);

        bool isThirdParty = checkIsThirdParty(pkgName, args->uid);
        LOGD("Process: %s (uid: %d) -> isThirdParty: %s", pkgName.c_str(), args->uid, isThirdParty ? "YES" : "NO");
        
        applyFontFix(isThirdParty);
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

    void postServerSpecialize(const zygisk::ServerSpecializeArgs *args) override {
        LOGD("System Server started, disabling font overlay");
        applyFontFix(false);
    }

private:
    zygisk::Api *api = nullptr;
    JNIEnv *env = nullptr;

    bool checkIsThirdParty(const std::string& pkgName, int uid) {
        if (uid < 10000) return false;

        // 1. Check common system package prefixes (from 1.txt + common MIUI)
        if (pkgName == "android" || 
            pkgName.find("com.miui.") == 0 || 
            pkgName.find("miui.") == 0 || 
            pkgName.find("com.xiaomi.") == 0 || 
            pkgName.find("com.mi.") == 0 || 
            pkgName.find("com.android.") == 0) {
            return false;
        }

        // 2. Get ApplicationInfo flags via ServiceManager
        int flags = getAppFlags(pkgName);
        if (flags != -1) {
            bool isSystem = (flags & 0x1);          // FLAG_SYSTEM
            bool isUpdated = (flags & 0x80);        // FLAG_UPDATED_SYSTEM_APP
            
            // Smali logic: if (isSystem && !isUpdated) -> is_system_app (false)
            if (isSystem && !isUpdated) {
                return false;
            }
        } else {
            // If getAppFlags failed, default to system app for safety
            LOGD("getAppFlags failed for %s, assuming system app", pkgName.c_str());
            return false;
        }

        return true;
    }

    int getAppFlags(const std::string& pkgName) {
        jclass smClass = env->FindClass("android/os/ServiceManager");
        if (smClass == nullptr) { env->ExceptionClear(); return -1; }

        jmethodID getServiceMethod = env->GetStaticMethodID(smClass, "getService", "(Ljava/lang/String;)Landroid/os/IBinder;");
        jstring serviceName = env->NewStringUTF("package");
        jobject binder = env->CallStaticObjectMethod(smClass, getServiceMethod, serviceName);
        env->DeleteLocalRef(serviceName);
        if (binder == nullptr) { env->ExceptionClear(); return -1; }

        jclass stubClass = env->FindClass("android/content/pm/IPackageManager$Stub");
        if (stubClass == nullptr) { env->ExceptionClear(); return -1; }
        jmethodID asInterfaceMethod = env->GetStaticMethodID(stubClass, "asInterface", "(Landroid/os/IBinder;)Landroid/content/pm/IPackageManager;");
        jobject ipm = env->CallStaticObjectMethod(stubClass, asInterfaceMethod, binder);
        if (ipm == nullptr) { env->ExceptionClear(); return -1; }

        jclass ipmClass = env->GetObjectClass(ipm);
        // Common signatures for getApplicationInfo
        const char* signatures[] = {
            "(Ljava/lang/String;JI)Landroid/content/pm/ApplicationInfo;", // Modern Android
            "(Ljava/lang/String;II)Landroid/content/pm/ApplicationInfo;"  // Legacy Android
        };

        jmethodID getAiMethod = nullptr;
        for (const char* sig : signatures) {
            getAiMethod = env->GetMethodID(ipmClass, "getApplicationInfo", sig);
            if (getAiMethod != nullptr) break;
            env->ExceptionClear();
        }
        
        if (getAiMethod == nullptr) { return -1; }

        jstring jPkgName = env->NewStringUTF(pkgName.c_str());
        jobject appInfo = nullptr;
        
        // Try calling based on the found signature
        // We use userId 0 (owner)
        appInfo = env->CallObjectMethod(ipm, getAiMethod, jPkgName, (jlong)0, 0);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            // Try with int flags if long failed
            appInfo = env->CallObjectMethod(ipm, getAiMethod, jPkgName, (jint)0, 0);
        }
        env->DeleteLocalRef(jPkgName);

        if (appInfo == nullptr) { env->ExceptionClear(); return -1; }

        jclass aiClass = env->GetObjectClass(appInfo);
        jfieldID flagsField = env->GetFieldID(aiClass, "flags", "I");
        int flags = env->GetIntField(appInfo, flagsField);

        return flags;
    }

    bool setStaticBooleanField(const char *className, const char *fieldName, jboolean value) {
        jclass clazz = env->FindClass(className);
        if (clazz == nullptr) {
            env->ExceptionClear();
            return false;
        }

        jfieldID field = env->GetStaticFieldID(clazz, fieldName, "Z");
        if (field == nullptr) {
            env->ExceptionClear();
            return false;
        }

        env->SetStaticBooleanField(clazz, field, value);
        return true;
    }

    void applyFontFix(bool isThirdParty) {
        setStaticBooleanField("miui/util/font/FontSettings", "HAS_MIUI_VAR_FONT", JNI_FALSE);
        setStaticBooleanField("miui/util/font/FontSettings", "MIUI_OPTIMIZE_ENABLED", JNI_FALSE);
        setStaticBooleanField("miui/util/font/FontScaleUtil", "isCtsBlockListPackage", isThirdParty ? JNI_TRUE : JNI_FALSE);
    }
};

REGISTER_ZYGISK_MODULE(DisableMiFontOverlay)
