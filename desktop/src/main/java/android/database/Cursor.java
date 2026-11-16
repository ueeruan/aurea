package android.database;

/**
 * Cursor do Android em Java: os retornos ficam "de plataforma" (String!), como
 * no android.jar, e o código Kotlin do app compila igual.
 */
public interface Cursor extends java.io.Closeable {
    int FIELD_TYPE_NULL = 0;
    int FIELD_TYPE_INTEGER = 1;
    int FIELD_TYPE_FLOAT = 2;
    int FIELD_TYPE_STRING = 3;
    int FIELD_TYPE_BLOB = 4;

    int getCount();
    int getColumnCount();
    String[] getColumnNames();
    int getPosition();
    boolean isClosed();
    boolean moveToFirst();
    boolean moveToNext();
    boolean moveToPosition(int p);
    boolean moveToLast();
    int getColumnIndex(String name);
    int getColumnIndexOrThrow(String name);
    String getColumnName(int i);
    String getString(int i);
    long getLong(int i);
    int getInt(int i);
    float getFloat(int i);
    double getDouble(int i);
    byte[] getBlob(int i);
    boolean isNull(int i);
    int getType(int i);
}
