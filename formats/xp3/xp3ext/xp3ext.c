//2010年作成
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <locale.h>
#include <zlib.h>

#include "xp3ext.h"

static struct {
  FILE*              fp;
  struct xp3head     head;
  struct xp3filehead filehead;
  int                filenum;
  struct xp3fileinfo *fileinfos;
} xp3dat;

#define DEBUG 0

// デバッグ出力
void  debug(char *format, ...)
{
  va_list ap;

  if (!DEBUG)
    return;
  fprintf(stderr, "Debug: ");
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

// エラーメッセージを表示して終了する
void  error_exit(char *format, ...)
{
  va_list ap;

  fprintf(stderr, "Error: ");
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  exit(1);
}


// メモリ確保する関数。エラーチェック付き。
void *alloc(size_t siz)
{
  void *ret;
  if ((ret = malloc(siz)) == NULL)
    error_exit("alloc(%lld(=0x%08llx)) in %s()", siz, siz, __FUNCTION__);
  return ret;
}


// 圧縮データを、zlibを使って展開する
void  decode(void *dst, uint64_t dstsiz, void *src, uint64_t srcsiz)
{
  int err;
  unsigned long siz;

  if ((err = uncompress(dst, &siz, src, srcsiz)) != Z_OK || dstsiz != siz)
    error_exit("error code = %d, Fail to uncompress()", err);
}


#if 0
// wchar_t* を char* に変換する。Linuxのwchar_tと違うのでムリヤリ…
int  wcstombs_local(char *cs, wchar_t *wcs, size_t n)
{
  int i, idx;
  uint8_t *dst = (uint8_t*)cs, *src = (uint8_t*)wcs;

  for (i = 0, idx = 0; idx < n; i+=2) {
    dst[idx++] = src[i];
    if (src[i] == 0)
      break;
    // Shift-JISかEUC前提だけどええんやろか。UTF-32のこととかないんやろか。
    if (src[i] >= 0x80)
      dst[idx++] = src[i+1];
  }
  return idx;
}
#endif


// ファイルを作成するために必要なディレクトリを作る
void  mkdir_p(char *fnam)
{
  int  i;
  char dirnam[PATH_MAX+1];

  for (i = 1/*0は不要*/; i < strlen(fnam); i++) {
    if (fnam[i] == '/') {
      strncpy(dirnam, fnam, i);
      dirnam[i] = 0;
      if (mkdir(dirnam, 0755) != 0 && errno != EEXIST)
	error_exit("fail to mkdir(%s) in %s(%s)", dirnam, __FUNCTION__, fnam);
    }
  }
}


// xp3ファイルのファイルヘッダを読み込み、xp3dat.head に格納する
void  read_xp3header(void)
{
  int8_t  xp3magic[] = XP3MAGIC;
  int64_t tmpidx;

  debug("Enter %s()", __FUNCTION__);

  // マジックNo.チェック
  fidx(xp3dat.fp, 0);
  fload(xp3dat.fp, xp3dat.head.magic, sizeof(xp3dat.head.magic));
  if (memcmp(xp3dat.head.magic, xp3magic, sizeof(xp3magic)) != 0)
    error_exit("xp3magic is mis-match in %s()", __FUNCTION__);

  // 次の64bit数値で、2.28以前か2.30以降かを判別。いいのかこんなので。
  fint64(xp3dat.fp, tmpidx);
  if (tmpidx != CUSHIONIDX) {
    xp3dat.head.fileinfo_idx = tmpidx;
    // このとき、自動的に xp3dat.head.minor_version = 0 になる。
  } else {
    fint32(xp3dat.fp, xp3dat.head.minor_version);
    fint8 (xp3dat.fp, xp3dat.head.cushion_head);
    fint64(xp3dat.fp, xp3dat.head.cushion_idx_siz);

    fint64(xp3dat.fp, xp3dat.head.fileinfo_idx);
  }
  debug("fileinfo_idx = %08llx", xp3dat.head.fileinfo_idx);

  debug("Exit %s()", __FUNCTION__);
}


// セグメントを一つ読みこむ
struct xp3segment  read_a_segment(void *buf)
{
  struct xp3segment seg;
  mint32(buf, seg.flag);
  mint64(buf, seg.offset);
  mint64(buf, seg.orgsiz);
  mint64(buf, seg.datsiz);
  return seg;
}


// fileinfoを一つ読み込む
struct xp3fileinfo  read_a_fileinfo(void *buf, uint64_t bufsiz)
{
  struct xp3fileinfo fi;
  void *buftop = buf;
  int processed = 0;

  debug("Enter %s(buf, 0x%08llx)", __FUNCTION__, bufsiz);

  while (buf - buftop < bufsiz) {
    uint8_t  id[4];
    uint64_t siz;
    void     *bufsave = buf;

    // チャンクヘッド読み込み
    mstrid(buf, id);
    mint64(buf, siz);

    // IDごとに処理振り分け
    if (strncmp(id, FILEID, sizeof(FILEID)-1) == 0) {
      // "File"チャンク
      if (processed)
	break; // "File" を一度処理済なら、次の処理に回す
      debug("Process \"File\" chank(idx=%d)", buf-buftop-CHUNKHEADSIZ);
      processed = 1;
      strncpy(fi.fileid, id, sizeof(FILEID));
      fi.siz = siz;
    }
    else if (strncmp(id, INFOID, sizeof(INFOID)-1) == 0) {
      // "info"チャンク
      debug("Process \"info\" chank(idx=%d)", buf-buftop-CHUNKHEADSIZ);
      strncpy(fi.infoid, id, sizeof(INFOID));
      fi.infosiz = siz;

      mint32(buf, fi.flag);
      mint64(buf, fi.orgsiz);
      mint64(buf, fi.datsiz);

      // ファイル名(Wstring)を保存
      mint16(buf, fi.filenamsiz);
      fi.filenam = alloc(fi.filenamsiz*2 + 2); // 終端のために +2 は必要
      memcpy(fi.filenam, buf, fi.filenamsiz*2);
      ((uint8_t*)fi.filenam)[fi.filenamsiz*2]   = '\0'; // 念のため終端
      ((uint8_t*)fi.filenam)[fi.filenamsiz*2+1] = '\0';

      buf = bufsave + siz + CHUNKHEADSIZ;
    }
    else if (strncmp(id, SEGID, sizeof(SEGID)-1) == 0) {
      // "segm" チャンク
      int i;
      debug("Process \"segm\" chank(idx=%d)", buf-buftop-CHUNKHEADSIZ);
      strncpy(fi.segid, id, sizeof(SEGID));
      fi.segsiz = siz;
      // セグメント読み込み
      fi.segary = alloc(fi.segsiz);
      debug("segment number = %d", fi.segsiz/SEGMENTSIZ);

      for (i = 0; i < fi.segsiz/SEGMENTSIZ; i++) {
	fi.segary[i] = read_a_segment(buf);
	buf += SEGMENTSIZ;
      }

      buf = bufsave + siz + CHUNKHEADSIZ;
    }
    else if (strncmp(id, ADLRID, sizeof(ADLRID)-1) == 0) {
      // "adlr" チャンク
      // 現在はチェックしないので黙ってスキップ
      buf = bufsave + siz + CHUNKHEADSIZ;
    }
    else {
      // 知らないチャンクだった場合はスキップ
      fprintf(stderr, "Unknown id(\"%.4s\"), skip(idx=%d)\n", id, buf-buftop-CHUNKHEADSIZ);
      buf = bufsave + siz + CHUNKHEADSIZ;
    }
  }

  debug("Exit %s()", __FUNCTION__);
  return fi;
}


// ファイル管理部を読み込む
void  read_fileinfos(void)
{
  int  i;
  void *buf, *bufsave;
  struct xp3filehead *fhp = &xp3dat.filehead;

  debug("Enter %s()", __FUNCTION__);

  fidx(xp3dat.fp, xp3dat.head.fileinfo_idx);
  fint8(xp3dat.fp, fhp->flag);

  // 圧縮フラグチェック
  if (!is_compressed(*fhp)) {
    // 非圧縮の場合
    fint64(xp3dat.fp, fhp->orgsiz);
    buf = bufsave = alloc(fhp->orgsiz);
    fload(xp3dat.fp, buf, fhp->orgsiz);
  } else {
    // 圧縮の場合
    void *cmpbuf;
    fint64(xp3dat.fp, fhp->datsiz);
    fint64(xp3dat.fp, fhp->orgsiz);
    buf = bufsave = alloc(fhp->orgsiz);
    cmpbuf = alloc(fhp->datsiz);
    // zlib展開
    fload(xp3dat.fp, cmpbuf, fhp->datsiz);
    decode(buf, fhp->orgsiz, cmpbuf, fhp->datsiz);
    free(cmpbuf);
  }
  // この時点で buf に展開済み管理部が格納されている
  // ファイル管理情報を一つづつ読み込む
  xp3dat.fileinfos = NULL;
  for (i = 0; buf-bufsave < fhp->orgsiz; i++) {
    struct xp3fileinfo fi;
    fi = read_a_fileinfo(buf, fhp->orgsiz - (buf-bufsave));
    xp3dat.fileinfos = realloc(xp3dat.fileinfos, sizeof(fi)*(i+1));
    if (xp3dat.fileinfos == NULL)
      error_exit("realloc fileinfo in %s", __FUNCTION__);
    xp3dat.fileinfos[i] = fi;
    buf += CHUNKHEADSIZ + fi.siz;
  }
  xp3dat.filenum = i;
  free(bufsave);

  debug("Exit %s()", __FUNCTION__);
}
  

// セグメントを一つファイルに展開する
void  extract_a_segment(struct xp3segment seg, FILE *fp)
{
  void  *dst;

  debug("Enter %s()", __FUNCTION__);

  fidx(xp3dat.fp, seg.offset);
  dst = alloc(seg.orgsiz);
  if (!is_compressed(seg)) {
    // セグメントが非圧縮だった場合
    fload(xp3dat.fp, dst, seg.orgsiz);
  } else {
    // セグメントが圧縮されていた場合
    void *src;
    src = alloc(seg.datsiz);
    fload(xp3dat.fp, src, seg.datsiz);
    decode(dst, seg.orgsiz, src, seg.datsiz);
    free(src);
  }
  fsave(fp, dst, seg.orgsiz);
  free(dst);

  debug("Exit %s()", __FUNCTION__);
}


// ファイルを一つ展開する
void  extract_a_file(struct xp3fileinfo fi)
{
  FILE *fp;
  char fnam[FILENAME_MAX+1];
  int  fnamsiz, i;

  debug("Enter %s()", __FUNCTION__);

  // ファイル名をWstringから文字列へ復元
  if ((fnamsiz = wcstombs(fnam, fi.filenam, FILENAME_MAX)) <= 0)
    error_exit("in wcstombs(,,%d) in %s()", fi.filenamsiz, __FUNCTION__);
  fnam[fnamsiz] = '\0';
  printf("Extracting file \"%s\"\n", fnam);

  // プロテクトされていても男らしく無視する
  if (is_protected(fi))
    printf("This file is protected, though I do not care:-9\n");

  // ファイル展開
  mkdir_p(fnam);
  if ((fp = fopen(fnam, "w")) == NULL) {
    // 失敗した場合はスキップするだけ
    fprintf(stderr, "Skip: fail to fopen(\"%s\", \"w\") in %s()", fnam, __FUNCTION__);
  } else {
    for (i = 0; i < fi.segsiz/SEGMENTSIZ; i++)
      extract_a_segment(fi.segary[i], fp);
    fclose(fp);
  }

  debug("Exit %s()", __FUNCTION__);
}


// ファイル群を展開する
void  extract_files(void)
{
  int  i;

  debug("Enter %s()", __FUNCTION__);

  for (i = 0; i < xp3dat.filenum; i++) {
    extract_a_file(xp3dat.fileinfos[i]);
  }

  debug("Exit %s()", __FUNCTION__);
}


// メインルーチン
int  main(int argc, char *argv[])
{
  printf("XP3 extractor [%s] version %s %s for Linux/Cygwin by KAICHO\n", argv[0], XP3EXT_VERSION, XP3EXT_DATE);
  if (argc != 2)
    error_exit("argument number\nxp3ext <.xp3file>");

  // ファイル名をSJISにするためにこんなことをしてしまう。いいのか。
  setlocale(LC_ALL, "ja_JP.SJIS");

  // まずデータを全てをクリア
  memset(&xp3dat, 0, sizeof(xp3dat.head));

  // ファイルオープン
  if ((xp3dat.fp = fopen(argv[1], "r")) == NULL)
    error_exit("open file(%s)", argv[1]);

  // ヘッダ読み込み
  read_xp3header();
  // ファイル情報読み込み
  read_fileinfos();

  // ファイル展開
  extract_files();

  fclose(xp3dat.fp);
  return 0;
}

