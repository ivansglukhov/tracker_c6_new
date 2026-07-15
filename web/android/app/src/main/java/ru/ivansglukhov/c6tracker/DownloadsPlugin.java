package ru.ivansglukhov.c6tracker;

import android.Manifest;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import com.getcapacitor.JSObject;
import com.getcapacitor.PermissionState;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;
import com.getcapacitor.annotation.Permission;
import com.getcapacitor.annotation.PermissionCallback;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

@CapacitorPlugin(
    name = "Downloads",
    permissions = @Permission(alias = "legacyStorage", strings = { Manifest.permission.WRITE_EXTERNAL_STORAGE })
)
public class DownloadsPlugin extends Plugin {
    @PluginMethod
    public void saveText(PluginCall call) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q &&
            getPermissionState("legacyStorage") != PermissionState.GRANTED) {
            requestPermissionForAlias("legacyStorage", call, "legacyPermissionCallback");
            return;
        }
        writeText(call);
    }

    @PermissionCallback
    private void legacyPermissionCallback(PluginCall call) {
        if (getPermissionState("legacyStorage") != PermissionState.GRANTED) {
            call.reject("Нет разрешения на запись в папку Загрузки");
            return;
        }
        writeText(call);
    }

    private void writeText(PluginCall call) {
        String fileName = call.getString("fileName");
        String mimeType = call.getString("mimeType", "text/plain");
        String content = call.getString("content");
        if (fileName == null || !fileName.matches("[A-Za-z0-9._-]+") || content == null) {
            call.reject("Некорректные параметры файла");
            return;
        }

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                writeWithMediaStore(call, fileName, mimeType, content);
            } else {
                writeLegacy(call, fileName, content);
            }
        } catch (Exception error) {
            call.reject("Не удалось сохранить файл: " + error.getMessage(), error);
        }
    }

    private void writeWithMediaStore(PluginCall call, String fileName, String mimeType, String content) throws Exception {
        ContentResolver resolver = getContext().getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, fileName);
        values.put(MediaStore.MediaColumns.MIME_TYPE, mimeType);
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/C6 Tracker");
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (uri == null) throw new IllegalStateException("Android не создал файл");
        try {
            try (OutputStream stream = resolver.openOutputStream(uri, "w")) {
                if (stream == null) throw new IllegalStateException("Android не открыл файл");
                stream.write(content.getBytes(StandardCharsets.UTF_8));
            }
            ContentValues ready = new ContentValues();
            ready.put(MediaStore.MediaColumns.IS_PENDING, 0);
            resolver.update(uri, ready, null, null);
            resolve(call, uri.toString());
        } catch (Exception error) {
            resolver.delete(uri, null, null);
            throw error;
        }
    }

    @SuppressWarnings("deprecation")
    private void writeLegacy(PluginCall call, String fileName, String content) throws Exception {
        File directory = new File(
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
            "C6 Tracker"
        );
        if (!directory.exists() && !directory.mkdirs()) throw new IllegalStateException("Не создан каталог C6 Tracker");
        File file = new File(directory, fileName);
        try (OutputStream stream = new FileOutputStream(file, false)) {
            stream.write(content.getBytes(StandardCharsets.UTF_8));
        }
        resolve(call, Uri.fromFile(file).toString());
    }

    private void resolve(PluginCall call, String uri) {
        JSObject result = new JSObject();
        result.put("uri", uri);
        call.resolve(result);
    }
}
