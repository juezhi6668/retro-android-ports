package com.google.android.googlelogin;

import android.content.Context;

/**
 * Pure-AOSP Donut build stub.
 *
 * The real GoogleLoginServiceBlockingHelper ships in the proprietary
 * GoogleLoginService (vendor/google), which is not part of AOSP. Only the
 * CalendarProviderTests apk references it, so this stub exists solely to let
 * a pure-AOSP Donut eng build complete. It is never used at runtime on a
 * device without the proprietary service (it just throws).
 */
public class GoogleLoginServiceBlockingHelper {
    public static String getAccount(Context context, boolean requireGoogleLogin)
            throws GoogleLoginServiceNotFoundException {
        throw new GoogleLoginServiceNotFoundException("GoogleLoginService not present (AOSP stub)");
    }
}
