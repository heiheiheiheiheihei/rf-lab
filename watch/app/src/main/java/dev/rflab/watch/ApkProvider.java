package dev.rflab.watch;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;

/**
 * Hands a verified APK under {@code cache/update} to the system installer as a
 * {@code content://} URI. Nothing else is exposed, read-only.
 */
public final class ApkProvider extends ContentProvider {
    static final String AUTHORITY = "dev.rflab.watch.apk";

    static Uri uriFor(Context context, File apk) {
        return new Uri.Builder().scheme("content").authority(AUTHORITY)
                .appendPath(apk.getName()).build();
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) {
            throw new FileNotFoundException("read only");
        }
        String name = uri.getLastPathSegment();
        if (name == null || !name.endsWith(".apk")) {
            throw new FileNotFoundException("not an apk");
        }
        File dir = new File(getContext().getCacheDir(), "update");
        File file = new File(dir, name);
        try {
            if (!file.getCanonicalPath().startsWith(dir.getCanonicalPath() + File.separator)) {
                throw new FileNotFoundException("outside update dir");
            }
        } catch (IOException e) {
            throw new FileNotFoundException("bad path");
        }
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        return null;
    }

    @Override
    public String getType(Uri uri) {
        return "application/vnd.android.package-archive";
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }
}
