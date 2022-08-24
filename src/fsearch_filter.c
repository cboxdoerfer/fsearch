#include "fsearch_filter.h"

#include <glib/gi18n.h>
#include <stdlib.h>
#include <string.h>

FsearchFilter *
fsearch_filter_new(const char *name, const char *macro, const char *query, FsearchQueryFlags flags) {
    g_assert(name);

    FsearchFilter *filter = calloc(1, sizeof(FsearchFilter));
    g_assert(filter);

    filter->name = strdup(name);
    filter->macro = strdup(macro ? macro : "");
    filter->query = strdup(query ? query : "");
    filter->flags = flags;
    filter->ref_count = 1;
    return filter;
}

bool
fsearch_filter_cmp(FsearchFilter *filter_1, FsearchFilter *filter_2) {
    g_assert(filter_1);
    g_assert(filter_2);

    if (strcmp(filter_1->name, filter_2->name) != 0) {
        return false;
    }
    if (strcmp(filter_1->macro, filter_2->macro) != 0) {
        return false;
    }
    if (strcmp(filter_1->query, filter_2->query) != 0) {
        return false;
    }
    if (filter_1->flags != filter_2->flags) {
        return false;
    }
    return true;
}

FsearchFilter *
fsearch_filter_copy(FsearchFilter *filter) {
    if (!filter) {
        return NULL;
    }
    return fsearch_filter_new(filter->name, filter->macro, filter->query, filter->flags);
}

static void
fsearch_filter_free(FsearchFilter *filter) {
    g_clear_pointer(&filter->name, free);
    g_clear_pointer(&filter->macro, free);
    g_clear_pointer(&filter->query, free);
    g_clear_pointer(&filter, free);
}

FsearchFilter *
fsearch_filter_ref(FsearchFilter *filter) {
    if (!filter || g_atomic_int_get(&filter->ref_count) <= 0) {
        return NULL;
    }
    g_atomic_int_inc(&filter->ref_count);
    return filter;
}

void
fsearch_filter_unref(FsearchFilter *filter) {
    if (!filter || g_atomic_int_get(&filter->ref_count) <= 0) {
        return;
    }
    if (g_atomic_int_dec_and_test(&filter->ref_count)) {
        g_clear_pointer(&filter, fsearch_filter_free);
    }
}

static const char *file_filter = "file:";
static const char *folder_filter = "folder:";
static const char *application_filter = "ext:desktop;DESKTOP";
static const char *document_filter = "ext:c;chm;cpp;csv;cxx;doc;docm;docx;dot;dotm;dotx;h;hpp;htm;html;hxx;ini;java;"
                                     "lua;mht;mhtml;ods;odt;odp;pdf;potx;potm;ppam;ppsm;ppsx;pps;ppt;pptm;pptx;rtf;"
                                     "sldm;sldx;thmx;txt;vsd;vsdx;wpd;wps;wri;xlam;xls;xlsb;xlsm;xlsx;xltm;xltx;xml;C;"
                                     "CHM;"
                                     "CPP;CSV;CXX;DOC;DOCM;DOCX;DOT;DOTM;DOTX;H;HPP;HTM;HTML;HXX;INI;JAVA;LUA;MHT;"
                                     "MHTML;ODS;ODT;ODP;PDF;POTX;POTM;PPAM;PPSM;PPSX;PPS;PPT;PPTM;PPTX;RTF;SLDM;SLDX;"
                                     "THMX;TXT;VSD;VSDX;WPD;WPS;WRI;XLAM;XLS;XLSB;XLSM;XLSX;XLTM;XLTX;XML";
static const char *audio_filter = "ext:aac;ac3;aif;aifc;aiff;au;cda;dts;fla;flac;it;m1a;m2a;m3u;m4a;mid;midi;mka;mod;"
                                  "mp2;mp3;mpa;ogg;opus;ra;rmi;spc;rmi;snd;umx;voc;wav;wma;xm;AAC;AC3;AIF;AIFC;AIFF;AU;"
                                  "CDA;DTS;FLA;FLAC;IT;M1A;M2A;M3U;M4A;MID;MIDI;MKA;MOD;MP2;MP3;MPA;OGG;OPUS;RA;RMI;"
                                  "SPC;RMI;SND;UMX;VOC;WAV;WMA;XM";
static const char *image_filter = "ext:ani;bmp;gif;ico;jpe;jpeg;jpg;pcx;png;psd;tga;tif;tiff;webp;wmf;ANI;BMP;GIF;ICO;"
                                  "JPE;JPEG;JPG;PCX;PNG;PSD;TGA;TIF;TIFF;WEBP;WMF";
static const char *video_filter = "ext:3g2;3gp;3gp2;3gpp;amr;amv;asf;avi;bdmv;bik;d2v;divx;drc;dsa;dsm;dss;dsv;evo;f4v;"
                                  "flc;fli;flic;flv;hdmov;ifo;ivf;m1v;m2p;m2t;m2ts;m2v;m4b;m4p;m4v;mkv;mp2v;mp4;mp4v;"
                                  "mpe;mpeg;mpg;mpls;mpv2;mpv4;mov;mts;ogm;ogv;pss;pva;qt;ram;ratdvd;rm;rmm;rmvb;roq;"
                                  "rpm;smil;smk;swf;tp;tpr;ts;vob;vp6;webm;wm;wmp;wmv;3G2;3GP;3GP2;3GPP;AMR;AMV;ASF;"
                                  "AVI;BDMV;BIK;D2V;DIVX;DRC;DSA;DSM;DSS;DSV;EVO;F4V;FLC;FLI;FLIC;FLV;HDMOV;IFO;IVF;"
                                  "M1V;M2P;M2T;M2TS;M2V;M4B;M4P;M4V;MKV;MP2V;MP4;MP4V;MPE;MPEG;MPG;MPLS;MPV2;MPV4;MOV;"
                                  "MTS;OGM;OGV;PSS;PVA;QT;RAM;RATDVD;RM;RMM;RMVB;ROQ;RPM;SMIL;SMK;SWF;TP;TPR;TS;VOB;"
                                  "VP6;WEBM;WM;WMP;WMV";
static const char *archive_filter = "ext:7z;ace;arj;bz2;cab;gz;gzip;jar;r00;r01;r02;r03;r04;r05;r06;r07;r08;r09;r10;"
                                    "r11;r12;r13;r14;r15;r16;r17;r18;r19;r20;r21;r22;r23;r24;r25;r26;r27;r28;r29;rar;"
                                    "tar;tgz;z;zip;7Z;ACE;ARJ;BZ2;CAB;GZ;GZIP;JAR;R00;R01;R02;R03;R04;R05;R06;R07;R08;"
                                    "R09;R10;R11;R12;R13;R14;R15;R16;R17;R18;R19;R20;R21;R22;R23;R24;R25;R26;R27;R28;"
                                    "R29;RAR;TAR;TGZ;Z;ZIP";

GList *
fsearch_filter_get_default() {
    GList *filters = NULL;
    filters = g_list_append(filters, fsearch_filter_new(_("All"), NULL, NULL, 0));
    filters = g_list_append(filters, fsearch_filter_new(_("Folders"), NULL, folder_filter, 0));
    filters = g_list_append(filters, fsearch_filter_new(_("Files"), NULL, file_filter, 0));
    filters = g_list_append(filters,
                            fsearch_filter_new(_("Applications"), "app", application_filter, QUERY_FLAG_MATCH_CASE));
    filters = g_list_append(filters, fsearch_filter_new(_("Archives"), "archive", archive_filter, QUERY_FLAG_MATCH_CASE));
    filters = g_list_append(filters, fsearch_filter_new(_("Audio"), "audio", audio_filter, QUERY_FLAG_MATCH_CASE));
    filters = g_list_append(filters, fsearch_filter_new(_("Documents"), "doc", document_filter, QUERY_FLAG_MATCH_CASE));
    filters = g_list_append(filters, fsearch_filter_new(_("Pictures"), "pic", image_filter, QUERY_FLAG_MATCH_CASE));
    filters = g_list_append(filters, fsearch_filter_new(_("Videos"), "video", video_filter, QUERY_FLAG_MATCH_CASE));

    return filters;
}
