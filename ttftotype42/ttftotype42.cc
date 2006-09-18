/* ttftotype42.cc -- driver for translating TrueType fonts to Type 42 fonts
 *
 * Copyright (c) 2006 Eddie Kohler
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version. This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <efont/t1rw.hh>
#include <efont/t1font.hh>
#include <efont/t1item.hh>
#include <lcdf/clp.h>
#include <lcdf/error.hh>
#include <efont/cff.hh>
#include <efont/otf.hh>
#include <efont/otfname.hh>
#include <efont/otfpost.hh>
#include <efont/otfcmap.hh>
#include <efont/ttfcs.hh>
#include <lcdf/md5.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#ifdef HAVE_CTIME
# include <time.h>
#endif
#if defined(_MSDOS) || defined(_WIN32)
# include <fcntl.h>
# include <io.h>
#endif

using namespace Efont;

#define VERSION_OPT	301
#define HELP_OPT	302
#define QUIET_OPT	303
#define OUTPUT_OPT	306

Clp_Option options[] = {
    { "help", 'h', HELP_OPT, 0, 0 },
    { "output", 'o', OUTPUT_OPT, Clp_ArgString, 0 },
    { "quiet", 'q', QUIET_OPT, 0, Clp_Negate },
    { "version", 'v', VERSION_OPT, 0, 0 },
};


static const char *program_name;
static bool binary = true;


void
usage_error(ErrorHandler *errh, const char *error_message, ...)
{
    va_list val;
    va_start(val, error_message);
    if (!error_message)
	errh->message("Usage: %s [OPTIONS] [FONTFILE [OUTPUTFILE]]", program_name);
    else
	errh->verror(ErrorHandler::ERR_ERROR, String(), error_message, val);
    errh->message("Type %s --help for more information.", program_name);
    exit(1);
}

void
usage()
{
    printf("\
'Cfftot1' translates a PostScript font from the Compact Font Format (CFF) to\n\
the usual Type 1 format. The input file should be either a raw CFF font or a\n\
PostScript-flavored OpenType font. The result, which is usually written to the\n\
standard output, is written in PFB or PFA format.\n\
\n\
Usage: %s [OPTIONS] [FONTFILE [OUTPUTFILE]]\n\
\n\
Options:\n\
  -o, --output=FILE            Write output to FILE.\n\
  -q, --quiet                  Do not generate any error messages.\n\
  -h, --help                   Print this message and exit.\n\
  -v, --version                Print version number and exit.\n\
\n\
Report bugs to <kohler@cs.ucla.edu>.\n", program_name);
}


// MAIN

static const char * const t42_tables[] = {
    "cvt ", "fpgm", "glyf", "head", "hhea", "hmtx",
    "loca", "maxp", "prep", "vhea", "vmtx", 0
};

struct NameId {
    const char *name;
    int nameid;
};

static const NameId fontinfo_names[] = {
    { "version", OpenType::Name::N_VERSION },
    { "Notice", OpenType::Name::N_TRADEMARK },
    { "Copyright", OpenType::Name::N_COPYRIGHT },
    { "FullName", OpenType::Name::N_FULLNAME },
    { "FamilyName", OpenType::Name::N_FAMILY },
    { "Weight", OpenType::Name::N_SUBFAMILY },
    { 0, 0 }
};

static void
fprint_sfnts(FILE *f, const String &data, bool glyf, const OpenType::Font &font)
{
    if (glyf && data.length() >= 65535) {
	OpenType::Data head = font.table("head");
	OpenType::Data loca = font.table("loca");
	bool loca_long = (head.length() >= 52 && head.u16(50) != 0);
	int loca_size = (loca_long ? 4 : 2);
	uint32_t first_offset = 0;
	for (int i = 1; i * loca_size < loca.length(); i++) {
	    uint32_t next_offset = (loca_long ? loca.u32(4*i) : loca.u16(2*i) * 2);
	    if (next_offset - first_offset >= 65535) {
		uint32_t prev_offset = (loca_long ? loca.u32(4*i - 4) : loca.u16(2*i - 2) * 2);
		fprint_sfnts(f, data.substring(first_offset, prev_offset - first_offset), false, font);
		first_offset = prev_offset;
	    }
	}
	fprint_sfnts(f, data.substring(first_offset), false, font);
	return;
    }

    fputc('<', f);
    const uint8_t *s = data.udata();
    for (int i = 0; i < data.length(); i++) {
	if (i && (i % 38) == 0)
	    fputc('\n', f);
	fputc("0123456789ABCDEF"[(s[i] >> 4) & 0xF], f);
	fputc("0123456789ABCDEF"[s[i] & 0xF], f);
    }
    if ((data.length() % 38) == 0)
	fputc('\n', f);
    fputs("00>\n", f);
}

static void
do_file(const char *infn, const char *outfn, ErrorHandler *errh)
{
    FILE *f;
    if (!infn || strcmp(infn, "-") == 0) {
	f = stdin;
	infn = "<stdin>";
#if defined(_MSDOS) || defined(_WIN32)
	_setmode(_fileno(f), _O_BINARY);
#endif
    } else if (!(f = fopen(infn, "rb")))
	errh->fatal("%s: %s", infn, strerror(errno));
  
    int c = getc(f);
    ungetc(c, f);

    if (c == EOF)
	errh->fatal("%s: empty file", infn);

    StringAccum sa(150000);
    while (!feof(f)) {
	int forward = fread(sa.reserve(32768), 1, 32768, f);
	sa.forward(forward);
    }
    if (f != stdin)
	fclose(f);

    LandmarkErrorHandler cerrh(errh, infn);
    OpenType::Font otf(sa.take_string(), &cerrh);
    if (!otf.ok() || !otf.check_checksums(&cerrh))
	return;
    if (otf.table("CFF"))
	cerrh.fatal("CFF-flavored OpenType font not suitable for Type 42");

    OpenType::Name name(otf.table("name"), &cerrh);
    OpenType::Data head_data = otf.table("head");
    if (!otf.table("glyf") || head_data.length() <= 52 || !name.ok())
	cerrh.fatal("font appears to lack required tables");

    // create reduced font
    Vector<OpenType::Tag> tags;
    Vector<String> tables;
    for (const char * const *table = t42_tables; *table; table++)
	if (String s = otf.table(*table)) {
	    tags.push_back(*table);
	    tables.push_back(s);
	}
    OpenType::Font reduced_font = OpenType::Font::make(true, tags, tables);
    
    // output file
    if (!outfn || strcmp(outfn, "-") == 0) {
	f = stdout;
	outfn = "<stdout>";
    } else if (!(f = fopen(outfn, "wb")))
	errh->fatal("%s: %s", outfn, strerror(errno));

#if defined(_MSDOS) || defined(_WIN32)
    if (binary)
	_setmode(_fileno(f), _O_BINARY);
#endif

    fprintf(f, "%%!\n");
    
    // get glyph names
    TrueTypeBoundsCharstringProgram ttbprog(&otf);
    Vector<PermString> gn;
    ttbprog.glyph_names(gn);
    OpenType::Post post(otf.table("post"));
    OpenType::Cmap cmap(otf.table("cmap"));
    double emunits = head_data.u16(18);

    // font opener
    fprintf(f, "%%!PS-TrueTypeFont-65536-%u-1\n", head_data.u32(4));
    if (post.ok())
	fprintf(f, "%%%%VMusage: %u %u\n", post.mem_type42(false), post.mem_type42(true));
    fprintf(f, "11 dict begin\n");
    fprintf(f, "/FontName /%s def\n", name.english_name(OpenType::Name::N_POSTSCRIPT).c_str());
    fprintf(f, "/FontType 42 def\n");
    fprintf(f, "/FontMatrix [1 0 0 1 0 0] def\n");
    fprintf(f, "/FontBBox {%d %d %d %d} readonly def\n",
	    /* head_data.s16(36), head_data.s16(38),
	       head_data.s16(40), head_data.s16(42)); */
	    (int) floor(head_data.s16(36) / emunits),
	    (int) floor(head_data.s16(38) / emunits),
	    (int) ceil(head_data.s16(40) / emunits),
	    (int) ceil(head_data.s16(42) / emunits));
    fprintf(f, "/PaintType 0 def\n");

    // XUID (MD5 sum of font data)
    {
	MD5_CONTEXT md5;
	md5_init(&md5);
	md5_update(&md5, (const unsigned char *) reduced_font.data(), reduced_font.length());
	union {
	    unsigned char c[MD5_DIGEST_SIZE];
	    uint32_t u[MD5_DIGEST_SIZE / 4];
	} result;
	md5_final(&result.c[0], &md5);
	fprintf(f, "/XUID [42");
	for (int i = 0; i < MD5_DIGEST_SIZE / 4; i++)
	    fprintf(f, " 16#%X", result.u[i]);
	fprintf(f, "] def\n");
    }

    // FontInfo dictionary
    fprintf(f, "/FontInfo 10 dict dup begin\n");
    for (const NameId *n = fontinfo_names; n->name; n++)
	if (String s = name.english_name(n->nameid)) {
	    fprintf(f, "/%s (", n->name);
	    for (const char *x = s.begin(); x < s.end(); x++)
		if (*x == '(' || *x == '\\' || *x == ')')
		    fprintf(f, "\\%c", *x);
		else if (*x == '\n' || (*x >= ' ' && *x <= '~'))
		    fputc(*x, f);
		else
		    fprintf(f, "\\%03o", (unsigned char) *x);
	    fprintf(f, ") readonly def\n");
	}
    if (post.ok()) {
	fprintf(f, "/isFixedPitch %s def\n", (post.is_fixed_pitch() ? "true" : "false"));
	fprintf(f, "/ItalicAngle %g def\n", post.italic_angle());
	fprintf(f, "/UnderlinePosition %g def\n", (post.underline_position() - (post.underline_thickness() / 2)) / emunits);
	fprintf(f, "/UnderlineThickness %g def\n", post.underline_thickness() / emunits);
    }
    fprintf(f, "end readonly def\n");
    
    // encoding
    fprintf(f, "/Encoding 256 array\n0 1 255{1 index exch/.notdef put}for\n");
    for (int i = 0; i < 256; i++)
	if (OpenType::Glyph g = cmap.map_uni(i))
	    fprintf(f, "dup %d /%s put\n", i, gn[g].c_str());
    fprintf(f, "readonly def\n");

    // print 'sfnts' array
    OpenType::Data sfnts = reduced_font.data_string();
    fprintf(f, "/sfnts[\n");
    fprint_sfnts(f, sfnts.substring(0, OpenType::Font::HEADER_SIZE + OpenType::Font::TABLE_DIR_ENTRY_SIZE * reduced_font.ntables()), false, reduced_font);
    for (int i = 0; i < reduced_font.ntables(); i++) {
	int off = OpenType::Font::HEADER_SIZE + OpenType::Font::TABLE_DIR_ENTRY_SIZE * i;
	uint32_t offset = sfnts.u32(off + 8);
	uint32_t length = (sfnts.u32(off + 12) + 3) & ~3;
	fprint_sfnts(f, sfnts.substring(offset, length), sfnts.u32(off) == 0x676C7966 /*glyf*/, reduced_font);
    }
    fprintf(f, "] def\n");

    // print CharStrings data
    fprintf(f, "/CharStrings %d dict dup begin\n", ttbprog.nglyphs());
    for (int i = 0; i < gn.size(); i++)
	fprintf(f, "/%s %d def\n", gn[i].c_str(), i);
    fprintf(f, "end readonly def\n");

    // complete font
    fprintf(f, "FontName currentdict end definefont pop\n");

    fprintf(f, "/%s 100 selectfont 30 30 moveto (Hello! 9) show showpage\n", name.english_name(OpenType::Name::N_POSTSCRIPT).c_str());
    if (f != stdout)
	fclose(f);
    
#if 0

    // collect TT tables
    OpenType::Cmap cmap(otf.table("cmap"));
    OpenType::Post post(otf.table("post"));

    // collect glyph names
    Vector<PermString> glyph_names;
    if (post.ok())
	post.glyph_names(glyph_names);
    int nglyphs = (post.ok() ? post.nglyphs() : 0);
    if (OpenType::Data maxp = otf.table("maxp"))
	if (maxp.length() >= 6)
	    nglyphs = maxp.u16(4);
    
    // write header
    fprintf(f, "%!PS-TrueTypeFont-65536-%u-1\n", ntohl(head_begin[1]));
    if (post.ok())
	fprintf(f, "%%VMusage: %u %u\n", post.mem_type42(false), post.mem_type42(true));
    fprintf(f, "11 dict begin\n");
    fprintf(f, "/FontName /%s def\n", name.english_name(OpenType::Name::N_POSTSCRIPT));
    fprintf(f, "/Encoding 256 array\n\
0 1 255{1 index exch/.notdef put}for\n");
    for (int i = 0; i < 256; i++)
	if (OpenType::Glyph g = cmap.map_uni(i))
	    fprintf(f, "dup %d /index%d put\n", 
    if (String n = name.english_name(OpenType::Name::N_VERSION)) {
	
	if (n.find_left('\n') < 0 && n.find_left('\r') < 0 && n.find_left('\v'
	Type1PFBWriter t1w(f);
	font1->write(t1w);
    } else {
	Type1PFAWriter t1w(f);
	font1->write(t1w);
    }

    if (f != stdout)
	fclose(f);
    
    if (c == 'O')
	data = OpenType::Font(data, &cerrh).table("CFF");

    Cff *cff = new Cff(data, &cerrh);
    Cff::FontParent *fp = cff->font(name, &cerrh);
    if (errh->nerrors() == 0
	&& !(font = dynamic_cast<Cff::Font *>(fp)))
	errh->fatal("%s: CID-keyed fonts not supported", infn);

    if (errh->nerrors() > 0)
	return;
    
    Type1Font *font1 = create_type1_font(font, errh);

    if (!outfn || strcmp(outfn, "-") == 0) {
	f = stdout;
	outfn = "<stdout>";
    } else if (!(f = fopen(outfn, "wb")))
	errh->fatal("%s: %s", outfn, strerror(errno));

    if (binary) {
#if defined(_MSDOS) || defined(_WIN32)
	_setmode(_fileno(f), _O_BINARY);
#endif
	Type1PFBWriter t1w(f);
	font1->write(t1w);
    } else {
	Type1PFAWriter t1w(f);
	font1->write(t1w);
    }

    if (f != stdout)
	fclose(f);
#endif
}

int
main(int argc, char *argv[])
{
    Clp_Parser *clp =
	Clp_NewParser(argc, (const char * const *)argv, sizeof(options) / sizeof(options[0]), options);
    program_name = Clp_ProgramName(clp);
  
    ErrorHandler *errh = ErrorHandler::static_initialize(new FileErrorHandler(stderr, String(program_name) + ": "));
    const char *input_file = 0;
    const char *output_file = 0;
  
    while (1) {
	int opt = Clp_Next(clp);
	switch (opt) {

	  case QUIET_OPT:
	    if (clp->negated)
		errh = ErrorHandler::default_handler();
	    else
		errh = new SilentErrorHandler;
	    break;
      
	  case VERSION_OPT:
	    printf("cfftot1 (LCDF typetools) %s\n", VERSION);
	    printf("Copyright (C) 2002-2006 Eddie Kohler\n\
This is free software; see the source for copying conditions.\n\
There is NO warranty, not even for merchantability or fitness for a\n\
particular purpose.\n");
	    exit(0);
	    break;
      
	  case HELP_OPT:
	    usage();
	    exit(0);
	    break;

	  case OUTPUT_OPT:
	  output_file:
	    if (output_file)
		usage_error(errh, "output file specified twice");
	    output_file = clp->arg;
	    break;

	  case Clp_NotOption:
	    if (input_file && output_file)
		usage_error(errh, "too many arguments");
	    else if (input_file)
		goto output_file;
	    else
		input_file = clp->arg;
	    break;
      
	  case Clp_Done:
	    goto done;
      
	  case Clp_BadOption:
	    usage_error(errh, 0);
	    break;
      
	  default:
	    break;
      
	}
    }
  
  done:
    do_file(input_file, output_file, errh);
    
    return (errh->nerrors() == 0 ? 0 : 1);
}
