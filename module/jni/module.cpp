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
            disableMiFont(true);
            return;
        }
        std::string pkgName(nice_name);
        env->ReleaseStringUTFChars(args->nice_name, nice_name);

        bool isThirdParty = checkIsThirdParty(pkgName, args->uid);
        disableMiFont(isThirdParty);
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

    void postServerSpecialize(const zygisk::ServerSpecializeArgs *args) override {
        disableMiFont(false);
    }

private:
    zygisk::Api *api = nullptr;
    JNIEnv *env = nullptr;

    bool checkIsThirdParty(const std::string& pkgName, int uid) {
        if (uid < 10000) return false;

        // 1. Check package name prefixes (from 1.txt)
        if (pkgName == "android" || 
            pkgName.find("com.miui.") == 0 || 
            pkgName.find("com.xiaomi.") == 0 || 
            pkgName.find("com.android.") == 0) {
            return false;
        }

        // 2. Get ApplicationInfo flags via ServiceManager (the robust way)
        int flags = getAppFlags(pkgName);
        if (flags != -1) {
            bool isSystem = (flags & 0x1);          // FLAG_SYSTEM
            bool isUpdated = (flags & 0x80);        // FLAG_UPDATED_SYSTEM_APP
            
            // Smali logic: if (isSystem && !isUpdated) -> is_system_app
            if (isSystem && !isUpdated) {
                return false;
            }
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
        // getApplicationInfo(String packageName, long flags, int userId) - Note: flags type varies by Android version
        // We try the common signature first.
        jmethodID getAiMethod = env->GetMethodID(ipmClass, "getApplicationInfo", "(Ljava/lang/String;JI)Landroid/content/pm/ApplicationInfo;");
        if (getAiMethod == nullptr) {
            env->ExceptionClear();
            // Fallback for older Android versions where flags is int
            getAiMethod = env->GetMethodID(ipmClass, "getApplicationInfo", "(Ljava/lang/String;II)Landroid/content/pm/ApplicationInfo;");
        }
        
        if (getAiMethod == nullptr) { env->ExceptionClear(); return -1; }

        jstring jPkgName = env->NewStringUTF(pkgName.c_str());
        // userId 0 is owner/main user
        jobject appInfo = env->CallObjectMethod(ipm, getAiMethod, jPkgName, (jlong)0, 0);
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
        LOGD("Set %s.%s = %s", className, fieldName, value ? "true" : "false");
        return true;
    }

    void disableMiFont(bool isThirdParty) {
        setStaticBooleanField("miui/util/font/FontSettings", "HAS_MIUI_VAR_FONT", JNI_FALSE);
        setStaticBooleanField("miui/util/font/FontSettings", "MIUI_OPTIMIZE_ENABLED", JNI_FALSE);
        setStaticBooleanField("miui/util/font/FontScaleUtil", "isCtsBlockListPackage", isThirdParty ? JNI_TRUE : JNI_FALSE);
    }
};

REGISTER_ZYGISK_MODULE(DisableMiFontOverlay)
