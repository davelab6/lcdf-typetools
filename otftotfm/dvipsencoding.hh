#ifndef T1SICLE_DVIPSENCODING_HH
#define T1SICLE_DVIPSENCODING_HH
#include <efont/otfcmap.hh>
#include <efont/cff.hh>
#include <lcdf/hashmap.hh>
class GsubEncoding;

class DvipsEncoding { public:

    DvipsEncoding();

    static int parse_glyphlist(String);
    static int glyphname_unicode(const String &);
    static void glyphname_unicode(String, Vector<int> &);

    operator bool() const			{ return _e.size() > 0; }
    const String &name() const			{ return _name; }

    void encode(int, PermString);
    int encoding_of(PermString) const;
    bool encoded(int e) const;

    int parse(String filename, ErrorHandler *);

    // also modifies 'this':
    void make_literal_gsub_encoding(GsubEncoding &, Efont::Cff::Font *);
    void make_gsub_encoding(GsubEncoding &, const Efont::OpenType::Cmap &, Efont::Cff::Font * = 0);
    
    void apply_ligkern(GsubEncoding &, ErrorHandler *) const;
    
    enum { J_BAD = -1, J_NOKERN = 100, J_NOLIG = 101, J_NOLIGKERN = 102,
	   J_ALL = 0x7FFFFFFF };
    
  private:

    struct Ligature {
	int c1, c2, join, d;
    };

    String _name;
    Vector<PermString> _e;
    String _initial_comment;
    String _final_text;
    int _boundary_char;

    Vector<Ligature> _lig;
    HashMap<PermString, int> _unicoding_map;
    Vector<int> _unicoding;

    int parse_ligkern(const Vector<String> &);
    int parse_unicoding(const Vector<String> &);
    int parse_words(const String &, int (DvipsEncoding::*)(const Vector<String> &));
    void bad_codepoint(int);

    static PermString dot_notdef;

};

inline bool
DvipsEncoding::encoded(int e) const
{
    return e >= 0 && e < _e.size() && _e[e] != dot_notdef;
}

#endif