#include <android/log.h>
#include <sys/types.h>
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
        disableMiFont();
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

    void postServerSpecialize(const zygisk::ServerSpecializeArgs *args) override {
        disableMiFont();
    }

private:
    zygisk::Api *api = nullptr;
    JNIEnv *env = nullptr;

    bool setStaticBooleanField(const char *className, const char *fieldName, jboolean value) {
        jclass clazz = env->FindClass(className);
        if (clazz == nullptr) {
            env->ExceptionClear();
            LOGD("Class not found: %s", className);
            return false;
        }

        jfieldID field = env->GetStaticFieldID(clazz, fieldName, "Z");
        if (field == nullptr) {
            env->ExceptionClear();
            LOGD("Field not found: %s.%s", className, fieldName);
            return false;
        }

        env->SetStaticBooleanField(clazz, field, value);
        LOGD("Set %s.%s = %s", className, fieldName, value ? "true" : "false");
        return true;
    }

    void disableMiFont() {
        // Disable isMiuiFontEnabled(): HAS_MIUI_VAR_FONT && MIUI_OPTIMIZE_ENABLED && ...
        setStaticBooleanField("miui/util/font/FontSettings", "HAS_MIUI_VAR_FONT", JNI_FALSE);
        setStaticBooleanField("miui/util/font/FontSettings", "MIUI_OPTIMIZE_ENABLED", JNI_FALSE);

        // Android 16 (HyperOS 3.0): checkSupportMiuiFont() uses Flags.newHyperFont() path
        // which returns !isCtsBlockListPackage, bypassing HAS_MIUI_VAR_FONT entirely.
        // Setting isCtsBlockListPackage = true makes checkSupportMiuiFont() return false.
        setStaticBooleanField("miui/util/font/FontScaleUtil", "isCtsBlockListPackage", JNI_TRUE);
    }
};

REGISTER_ZYGISK_MODULE(DisableMiFontOverlay)
