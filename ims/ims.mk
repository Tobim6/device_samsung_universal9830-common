# Open-source IMS (VoLTE/VoWiFi): krazey's ImsStack/ImsMedia/CarrierSettings (Android 17 native).
# Only active when the forks are synced, so the tree still builds without them.
ifneq ($(and $(wildcard packages/modules/ImsStack/java/Android.bp),$(wildcard packages/modules/ImsMedia/sepolicy/system_ext/private/imsmedia.te),$(wildcard packages/apps/CarrierSettings/carrier_settings.mk)),)

$(call inherit-product, packages/modules/ImsMedia/imsmedia.mk)
$(call inherit-product, packages/apps/CarrierSettings/carrier_settings.mk)

PRODUCT_PACKAGES += \
    ImsStack \
    Iwlan \
    QualifiedNetworksService

PRODUCT_PACKAGES += \
    FrameworkResOverlayImsStack \
    ImsStackOverlayExynos9830 \
    TelephonyOverlayImsStack


# Without this system feature PhoneFactory never creates the ImsPhone and ImsManager refuses to work
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.telephony.ims.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.ims.xml


# Runtime permissions the stack needs (cell info listener); see permissions/default-permissions-imsstack.xml
PRODUCT_PACKAGES += \
    default-permissions_imsstack

endif
