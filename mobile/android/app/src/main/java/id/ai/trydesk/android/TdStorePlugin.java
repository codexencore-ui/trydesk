package id.ai.trydesk.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import com.getcapacitor.JSArray;
import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

// The phone's small secure store, the Android twin of ios/.../TdStorePlugin.swift. It holds the one secret the
// viewer app keeps: the access password of a device the user chose to remember (key "pw:<deviceId>").
//
// Each value is AES-256/GCM encrypted with a key that lives in the Android Keystore (never leaves it, is tied to
// this device, and is not extractable). Only the ciphertext + IV are written to SharedPreferences, so a rooted
// phone that reads the prefs file still cannot decrypt it. iOS uses the Keychain instead.
//
// mobile-shim.js maps the desktop renderer's savedPassword* calls onto this: get/set/remove, one key at a time,
// plus list() of the keys that start with a given prefix (used to drop everything when the user clears passwords).
@CapacitorPlugin(name = "TdStore")
public class TdStorePlugin extends Plugin {
    private static final String PREFS = "td_secure_store";
    private static final String ALIAS = "td_store_key";
    private static final String ANDROID_KEYSTORE = "AndroidKeyStore";

    private SharedPreferences prefs() { return getContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE); }

    private static String cleanKey(String raw) {
        if (raw == null || raw.isEmpty() || raw.length() > 80 || raw.indexOf('\n') >= 0) return null;
        return raw;
    }

    private SecretKey key() throws Exception {
        KeyStore ks = KeyStore.getInstance(ANDROID_KEYSTORE);
        ks.load(null);
        if (ks.containsAlias(ALIAS)) return (SecretKey) ks.getKey(ALIAS, null);
        KeyGenerator kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE);
        kg.init(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build());
        return kg.generateKey();
    }

    private String encrypt(String plain) throws Exception {
        Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
        c.init(Cipher.ENCRYPT_MODE, key());
        byte[] iv = c.getIV(), ct = c.doFinal(plain.getBytes(StandardCharsets.UTF_8));
        // one string: base64(iv) + ":" + base64(ciphertext). GCM's tag rides with the ciphertext.
        return Base64.encodeToString(iv, Base64.NO_WRAP) + ":" + Base64.encodeToString(ct, Base64.NO_WRAP);
    }

    private String decrypt(String stored) throws Exception {
        String[] parts = stored.split(":", 2);
        if (parts.length != 2) throw new Exception("malformed");
        byte[] iv = Base64.decode(parts[0], Base64.NO_WRAP), ct = Base64.decode(parts[1], Base64.NO_WRAP);
        Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
        c.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, iv));
        return new String(c.doFinal(ct), StandardCharsets.UTF_8);
    }

    @PluginMethod
    public void get(PluginCall call) {
        String key = cleanKey(call.getString("key", ""));
        if (key == null) { call.reject("invalid key"); return; }
        String stored = prefs().getString(key, null);
        if (stored == null) { JSObject r = new JSObject(); r.put("value", JSObject.NULL); call.resolve(r); return; }
        try {
            JSObject r = new JSObject(); r.put("value", decrypt(stored)); call.resolve(r);
        } catch (Exception e) {
            // The Keystore key can be gone (device restore, key reset): the value is unrecoverable, so drop it
            // and answer "nothing saved" instead of erroring on every later read.
            prefs().edit().remove(key).apply();
            JSObject r = new JSObject(); r.put("value", JSObject.NULL); call.resolve(r);
        }
    }

    @PluginMethod
    public void set(PluginCall call) {
        String key = cleanKey(call.getString("key", ""));
        String value = call.getString("value", null);
        if (key == null || value == null) { call.reject("invalid key or value"); return; }
        try {
            prefs().edit().putString(key, encrypt(value)).apply();
            call.resolve();
        } catch (Exception e) { call.reject("store write failed"); }
    }

    @PluginMethod
    public void remove(PluginCall call) {
        String key = cleanKey(call.getString("key", ""));
        if (key == null) { call.reject("invalid key"); return; }
        prefs().edit().remove(key).apply();
        call.resolve();
    }

    @PluginMethod
    public void list(PluginCall call) {
        String prefix = call.getString("prefix", "");
        JSArray arr = new JSArray();
        for (String k : prefs().getAll().keySet()) if (k.startsWith(prefix)) arr.put(k);
        JSObject r = new JSObject(); r.put("keys", arr); call.resolve(r);
    }
}
