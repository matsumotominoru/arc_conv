//2010年作成
#ifndef ___xp3ext_h
#define ___xp3ext_h

#define XP3EXT_VERSION "0.02"
#define XP3EXT_DATE    "2010/03/22"

#define XP3MAGIC {0x58,0x50,0x33,0x0d,0x0a,0x20,0x0a,0x1a,0x8b,0x67,0x01}
#define XP3MAGICSIZ 11
#define CUSHIONIDX  0x17

struct xp3head {
  int8_t  magic[XP3MAGICSIZ];
  int64_t cushion_idx;         // 今は 17 00 00 00 00 00 00 00 固定
  int32_t minor_version;       // 吉里吉里 2.30 から 01 00 00 00
  int8_t  cushion_head;        // 今は 0x80 固定
  int64_t cushion_idx_siz;     // 今は 00 00 00 00 00 00 00 00 固定
  int64_t fileinfo_idx;
};

// ファイル管理部ヘッダ
struct xp3filehead {
  int8_t  flag;                // 圧縮フラグ 0=管理部非圧縮、1=管理部圧縮
  int64_t datsiz;              // 圧縮管理部サイズ(非圧縮の場合は存在せず)
  int64_t orgsiz;              // 実際の管理部サイズ
};

// ファイル管理部中のセグメント構造体
struct xp3segment {
  int32_t flag;                 // 0 = 非圧縮、1 = 圧縮
  int64_t offset;               // そのセグメントデータのxp3ファイル中での位置
  int64_t orgsiz;               // データの元(展開後)サイズ
  int64_t datsiz;               // xp3ファイル中の(圧縮済)データサイズ
};
#define SEGMENTSIZ (4+8+8+8)  // sizeof(xp3segment)でない理由は…分かるよね？

// ファイル管理部中のファイルinfo構造体
struct xp3fileinfo {
  int8_t            fileid[4];  // "File"固定
  int64_t           siz;        // この管理部のサイズ
  int8_t            infoid[4];  // "info"固定
  int64_t           infosiz;    // infoサイズ
  int32_t           flag;       // 0=プロテクトなし 1<<31=プロテクトあり
  int64_t           orgsiz;     // 展開後のファイルサイズ
  int64_t           datsiz;     // 格納されているファイルサイズ
  int16_t           filenamsiz; // ファイル名長(ただし/2されている)
  wchar_t           *filenam;   // ファイル名(xp3中ではファイル名長分ある)
  int8_t            segid[4];   // "segm"固定
  int64_t           segsiz;     // セグメントサイズ=segment数*28(byte)
  struct xp3segment *segary;    // セグメント構造体の配列
  int8_t            adlerid[4]; // "adlr"固定
  int64_t           adlersiz;   // adlerサイズ(通常4固定)
  int32_t           adlersum;   // Adler-32 チェックサム値
};
#define FILEID   "File"
#define INFOID   "info"
#define SEGID    "segm"
#define ADLRID   "adlr"

#define is_compressed(fileinfo) ((fileinfo).flag & 1)
#define is_protected(fileinfo) ((fileinfo).flag & (1<<31))

#define CHUNKHEADSIZ 12       // チャンクヘッダサイズ = 4(id) + 8(siz)

// インデックスに移動する関数。エラーチェック付き
#define fidx(fp, idx) \
{ \
int ret; \
  if ((ret=fseek((fp), (idx), SEEK_SET)) != 0) \
    error_exit("%d fidx(fp,0x%08llx) in %s()\n", ret, (idx), __FUNCTION__); \
}

// データを読み込む関数。エラーチェック付き
#define fload(fp, buf, siz) \
{ \
  if (fread((buf), (siz), 1, (fp)) != 1) \
    error_exit("fload(fp, buf, %lld(=0x%08llx) in %s()\n", (siz), (siz), __FUNCTION__); \
}

// データを書き込む関数。エラーチェック付き
#define fsave(fp, buf, siz) \
{ \
  if (fwrite((buf), (siz), 1, (fp)) != 1) \
    error_exit("fsave(fp, buf, %lld) in %s()\n", (siz), __FUNCTION__); \
}

// 文字列IDを読み込む関数(4byteオンリー)。エラーチェック付き。
#define fstrid(fp, int8p) \
{ \
  if (fread(int8p, 4/* = sizeof(int8_t)*/, 1, (fp)) != 1) \
    error_exit("fstrid(fp, int8p) in %s()\n", __FUNCTION__); \
}

// 8bit整数を読み込む関数。エラーチェック付き。
#define fint8(fp, int8) \
{ \
  if (fread(&(int8), 1/* = sizeof(int8_t)*/, 1, (fp)) != 1) \
    error_exit("fint8(fp, int8) in %s()\n", __FUNCTION__); \
}

// 16bit整数を読み込む関数。エラーチェック付き。リトルエンディアン前提。
#define fint16(fp, int16) \
{ \
  if (fread(&(int16), 4/* = sizeof(int16_t)*/, 1, (fp)) != 1) \
    error_exit("fint16(fp, int16) in %s()\n", __FUNCTION__); \
}

// 32bit整数を読み込む関数。エラーチェック付き。リトルエンディアン前提。
#define fint32(fp, int32) \
{ \
  if (fread(&(int32), 4/* = sizeof(int32_t)*/, 1, (fp)) != 1) \
    error_exit("fint32(fp, int32) in %s()\n", __FUNCTION__); \
}

// 64bit整数を読み込む関数。エラーチェック付き。リトルエンディアン前提。
#define fint64(fp, int64) \
{ \
  if (fread(&(int64), 8/* = sizeof(int64_t)*/, 1, (fp)) != 1) \
    error_exit("fint64(fp, int64) in %s()\n", __FUNCTION__); \
}

// メモリから文字列IDを読み込む関数(4byteオンリー)
#define mstrid(buf, int8p) (memcpy((int8p), (buf), 4), (buf)+=4)

// メモリから8bit整数を読み込む関数、bufを進めて返す
#define mint8(buf, int8)   (((int8)  = *(uint8_t *)(buf)), (buf)++)

// メモリから16bit整数を読み込む関数、bufを進めて返す
#define mint16(buf, int16) (((int16) = *(uint16_t*)(buf)), (buf)+=2)

// メモリから16bit整数を読み込む関数、bufを進めて返す
#define mint32(buf, int32) (((int32) = *(uint32_t*)(buf)), (buf)+=4)

// メモリから16bit整数を読み込む関数、bufを進めて返す
#define mint64(buf, int64) (((int64) = *(uint64_t*)(buf)), (buf)+=8)


#endif // ___xp3ext_h
