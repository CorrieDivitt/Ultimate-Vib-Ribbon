// Vib-Ribbon on phones and tablets (task 4.20): SDL's activity with a folder picker.
//
// Android gives an app only the files the player picks, not the files beside them, so the
// player picks the folder holding the disc image (a .cue and its .bin files) and the game
// reads the files in it. The game's code (apps/game/android_import.cpp) calls pickFolder,
// waits for folderDone, then lists the folder with folderFiles.
package io.github.corriedivitt.vibribbon;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.util.ArrayList;

import org.libsdl.app.SDLActivity;

public class VibActivity extends SDLActivity {
    private static final int PICK_FOLDER = 4201;
    private static volatile boolean sDone = true;
    private static volatile String sTree;

    // Opens the system's folder picker (from the game's thread).
    public static void pickFolder() {
        sTree = null;
        sDone = false;
        final Activity activity = mSingleton;
        if (activity == null) { sDone = true; return; }
        activity.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                activity.startActivityForResult(intent, PICK_FOLDER);
            } catch (Exception e) {
                sDone = true;  // (no picker: as if cancelled)
            }
        });
    }

    public static boolean folderDone() { return sDone; }

    public static boolean folderPicked() { return sTree != null; }

    // The picked folder's files and those of the folders inside it (one level), as
    // groups of three: the folder's place ("" or the inner folder's id), the file's name,
    // and its content address. Empty when cancelled.
    public static String[] folderFiles() {
        ArrayList<String> out = new ArrayList<>();
        String tree = sTree;
        if (tree == null || mSingleton == null) return new String[0];
        try {
            Uri treeUri = Uri.parse(tree);
            String top = DocumentsContract.getTreeDocumentId(treeUri);
            ArrayList<String> inner = new ArrayList<>();
            list(treeUri, top, "", out, inner);
            for (String id : inner) list(treeUri, id, id, out, null);
        } catch (Exception e) {
            // (a provider that will not list: nothing)
        }
        return out.toArray(new String[0]);
    }

    private static void list(Uri treeUri, String parent, String place, ArrayList<String> out, ArrayList<String> folders) {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parent);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor c = mSingleton.getContentResolver().query(children, columns, null, null, null)) {
            while (c != null && c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                if (id == null || name == null) continue;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (folders != null) folders.add(id);
                    continue;
                }
                out.add(place);
                out.add(name);
                out.add(DocumentsContract.buildDocumentUriUsingTree(treeUri, id).toString());
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == PICK_FOLDER) {
            Uri uri = (resultCode == Activity.RESULT_OK && data != null) ? data.getData() : null;
            sTree = uri != null ? uri.toString() : null;
            sDone = true;
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }
}
