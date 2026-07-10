#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "diff-match-patch-cpp-stl/diff_match_patch.h"

struct BytesShim {
    static const char* PyArgFormat; // set below
    typedef Py_buffer PY_ARG_TYPE;
    typedef std::string STL_STRING_TYPE;

    // Extract the bytes data.
    static std::string to_string(Py_buffer& value) {
        auto buffer = (char*)malloc(value.len + 1);
        PyBuffer_ToContiguous(buffer, &value, value.len, 'C');
        PyBuffer_Release(&value);
        auto s = std::string(buffer, value.len);
        free(buffer);
        return s;
    }

    // Create PyString from underlying char array
    static PyObject* from_string(const std::string& value) {
        return PyBytes_FromStringAndSize(value.data(), value.size());
    }
};

const char* BytesShim::PyArgFormat = "s*";

struct UnicodeShim {
    static const char* PyArgFormat; // set below
    typedef PyObject* PY_ARG_TYPE;
    typedef std::u32string STL_STRING_TYPE;

    // Convert PyObject* to std::u32string....
    static std::u32string to_string(PyObject* value) {
        auto str = (char32_t*)PyUnicode_AsUCS4Copy(value);
        auto string = std::u32string(str, PyUnicode_GetLength(value));
        PyMem_Free(str);
        return string;
    }

    static PyObject* from_string(std::u32string value) {
        return PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, value.c_str(), value.size());
    }
};
const char* UnicodeShim::PyArgFormat = "U";

// Specialization of the DMP traits class for char32_t for u32string.
template <> struct diff_match_patch_traits<char32_t> : diff_match_patch_utf32_direct<char32_t> {
  static bool is_alnum(char32_t c) { return std::iswalnum(c)? true : false; }
  static bool is_digit(char32_t c) { return std::iswdigit(c)? true : false; }
  static bool is_space(char32_t c) { return std::iswspace(c)? true : false; }
  static int to_int(const char32_t* s) {
    std::string narrowed;
    while (*s && *s < CHAR_MAX)
        narrowed.append(1, static_cast<char>(*(s++)));
    return static_cast<int>(std::strtol(narrowed.c_str(), NULL, 10));
  }
  static char32_t from_wchar(wchar_t c) { return (char32_t)c; }
  static wchar_t to_wchar(char32_t c) { return c <= WCHAR_MAX ? (wchar_t)c : 0; }
  static std::u32string cs(const wchar_t* s) { return std::u32string(s, s + wcslen(s)); }
  static const char32_t eol = L'\n';
  static const char32_t tab = L'\t';
};

// Accessor to expose protected diff_linesToChars and Lines type.
template <class stringT, class traits = diff_match_patch_traits<typename stringT::value_type>>
struct DMPAccess : public diff_match_patch<stringT, traits> {
    typedef diff_match_patch<stringT, traits> DMP;
    using typename DMP::Lines;

    static void diff_linesToChars(typename DMP::string_t &text1,
                                  typename DMP::string_t &text2,
                                  Lines& lineArray) {
        DMP::diff_linesToChars(text1, text2, lineArray);
    }
};

// COMPUTATIONAL FUNCTIONS

template <class Shim>
static PyObject *
diff_match_patch__diff__impl(PyObject *self, PyObject *args, PyObject *kwargs)
{
    typename Shim::PY_ARG_TYPE a, b;
    float timelimit = 0.0;
    int checklines = 1;
    char* cleanupMode = NULL;
    int counts_only = 1;
    int as_patch = 0;
    char format_spec[64];

    static char *kwlist[] = {
        strdup("left_document"),
        strdup("right_document"),
        strdup("timelimit"),
        strdup("checklines"),
        strdup("cleanup"),
        strdup("counts_only"),
        strdup("as_patch"),
        NULL };

    sprintf(format_spec, "%s%s|fbzbb", Shim::PyArgFormat, Shim::PyArgFormat);
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, format_spec, kwlist,
                                     &a, &b,
                                     &timelimit, &checklines, &cleanupMode,
                                     &counts_only, &as_patch))
        return NULL;

    auto a_str = Shim::to_string(a),
         b_str = Shim::to_string(b);

    PyObject *ret = PyList_New(0);

    typedef diff_match_patch<typename Shim::STL_STRING_TYPE> DMP;
    DMP dmp;

    PyObject *opcodes[3];
    opcodes[dmp.DELETE] = PyUnicode_FromString("-");
    opcodes[dmp.INSERT] = PyUnicode_FromString("+");
    opcodes[dmp.EQUAL] = PyUnicode_FromString("=");

    typename DMP::Diffs diff;

    Py_BEGIN_ALLOW_THREADS /* RELEASE THE GIL */

    dmp.Diff_Timeout = timelimit;
    diff = dmp.diff_main(a_str, b_str, checklines);

    if (cleanupMode == NULL || strcmp(cleanupMode, "Semantic") == 0)
        dmp.diff_cleanupSemantic(diff);
    else if (strcmp(cleanupMode, "Efficiency") == 0)
        dmp.diff_cleanupEfficiency(diff);

    Py_END_ALLOW_THREADS /* ACQUIRE THE GIL */

    if (as_patch) {
        typename DMP::Patches patch = dmp.patch_make(a_str, diff);
        typename Shim::STL_STRING_TYPE patch_str = dmp.patch_toText(patch);

        return Shim::from_string(patch_str);
    }

    typename std::list<typename DMP::Diff>::const_iterator entryiter;
    for (entryiter = diff.begin(); entryiter != diff.end(); entryiter++) {
        typename DMP::Diff entry = *entryiter;

        PyObject* tuple = PyTuple_New(2);

        Py_INCREF(opcodes[entry.operation]); // we're going to reuse the object, so don't let SetItem steal the reference
        PyTuple_SetItem(tuple, 0, opcodes[entry.operation]);

        if (counts_only)
            PyTuple_SetItem(tuple, 1, PyLong_FromLong(entry.text.length()));
        else
            PyTuple_SetItem(tuple, 1, Shim::from_string(entry.text));

        PyList_Append(ret, tuple);
        Py_DECREF(tuple); // the list owns a reference now
    }

    // We're left with one extra reference.
    Py_DECREF(opcodes[dmp.DELETE]);
    Py_DECREF(opcodes[dmp.INSERT]);
    Py_DECREF(opcodes[dmp.EQUAL]);

    return ret;
}

template <class Shim>
static PyObject *
diff_match_patch__match__impl(PyObject *self, PyObject *args, PyObject *kwargs)
{
    typename Shim::PY_ARG_TYPE pattern, text;
    int loc;
    int match_distance = 1000;
    int match_maxbits = 32;
    float match_threshold = 0.5;
    char format_spec[64];

    static char *kwlist[] = {
        strdup("text"),
        strdup("pattern"),
        strdup("loc"),
        strdup("match_distance"),
        strdup("match_maxbits"),
        strdup("match_threshold"),
        NULL };

    sprintf(format_spec, "%s%si|iif", Shim::PyArgFormat, Shim::PyArgFormat);
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, format_spec, kwlist,
                                     &text, &pattern, &loc,
                                     &match_distance, &match_maxbits, &match_threshold)) {
        return NULL;
    }

    auto pattern_str = Shim::to_string(pattern),
         text_str = Shim::to_string(text);

    typedef diff_match_patch<typename Shim::STL_STRING_TYPE> DMP;
    DMP dmp;

    dmp.Match_Distance = match_distance;
    dmp.Match_MaxBits = match_maxbits;
    dmp.Match_Threshold = match_threshold;

    try {
        int index = dmp.match_main(text_str, pattern_str, loc);
        return Py_BuildValue("i", index);
    } catch (std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    } catch (typename Shim::STL_STRING_TYPE& s) {
        PyErr_SetObject(PyExc_RuntimeError, Shim::from_string(s));
        return NULL;
    }
}

template <class Shim>
static PyObject *
diff_match_patch__patch_apply__impl(PyObject *self, PyObject *args, PyObject *kwargs)
{
    typename Shim::PY_ARG_TYPE patch_text, text;
    float match_threshold = 0.5;
    int match_distance = 1000;
    float patch_delete_threshold = 0.5;
    char format_spec[64];

    static char *kwlist[] = {
        strdup("patch_text"),
        strdup("text"),
        strdup("match_threshold"),
        strdup("match_distance"),
        strdup("patch_delete_threshold"),
        NULL };

    sprintf(format_spec, "%s%s|fif", Shim::PyArgFormat, Shim::PyArgFormat);
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, format_spec, kwlist,
                                     &patch_text, &text,
                                     &match_threshold, &match_distance,
                                     &patch_delete_threshold))
        return NULL;

    auto patch_str = Shim::to_string(patch_text),
         text_str = Shim::to_string(text);

    typedef diff_match_patch<typename Shim::STL_STRING_TYPE> DMP;
    typedef typename Shim::STL_STRING_TYPE string_t;
    typedef typename string_t::value_type char_t;
    DMP dmp;

    dmp.Match_Threshold = match_threshold;
    dmp.Match_Distance = match_distance;
    dmp.Patch_DeleteThreshold = patch_delete_threshold;

    std::pair<string_t, std::vector<bool>> result;

    Py_BEGIN_ALLOW_THREADS /* RELEASE THE GIL */

    typename DMP::Patches patches = dmp.patch_fromText(patch_str);

    if (!patches.empty()) {
        // Deep copy patches
        typename DMP::Patches patchesCopy(patches);

        // Reimplement patch_addPadding + patch application here instead of
        // calling dmp.patch_apply(), because the upstream patch_addPadding has
        // a bug: in the "Add some padding on end of last diff" section it
        // writes `patches.front()` where it should be `patches.back()`.
        // The effect is that the trailing nullPadding is prepended to the
        // *first* patch's diffs (giving it an extra 4 chars of context) while
        // the *last* patch gets no trailing padding at all.  This corrupts
        // length1/length2 on the first patch, which throws off the delta
        // calculation and causes every subsequent patch_apply to fail.
        // See: diff_match_patch.h patch_addPadding(), around line 2117.
        short paddingLength = dmp.Patch_Margin;
        string_t nullPadding;
        for (short x = 1; x <= paddingLength; x++) {
            nullPadding += (char_t)x;
        }

        // Bump all patches forward
        for (typename DMP::Patches::iterator p = patchesCopy.begin(); p != patchesCopy.end(); ++p) {
            p->start1 += paddingLength;
            p->start2 += paddingLength;
        }

        // Add padding on start of first diff
        typename DMP::Patch &firstPatch = patchesCopy.front();
        typename DMP::Diffs &firstDiffs = firstPatch.diffs;
        if (firstDiffs.empty() || firstDiffs.front().operation != DMP::EQUAL) {
            firstDiffs.push_front(typename DMP::Diff(DMP::EQUAL, nullPadding));
            firstPatch.start1 -= paddingLength;
            firstPatch.start2 -= paddingLength;
            firstPatch.length1 += paddingLength;
            firstPatch.length2 += paddingLength;
        } else if (paddingLength > (int)firstDiffs.front().text.length()) {
            typename DMP::Diff &firstDiff = firstDiffs.front();
            int extraLength = paddingLength - (int)firstDiff.text.length();
            firstDiff.text = nullPadding.substr(firstDiff.text.length(), extraLength) + firstDiff.text;
            firstPatch.start1 -= extraLength;
            firstPatch.start2 -= extraLength;
            firstPatch.length1 += extraLength;
            firstPatch.length2 += extraLength;
        }

        // Add padding on end of LAST diff (correctly using back())
        typename DMP::Patch &lastPatch = patchesCopy.back();
        typename DMP::Diffs &lastDiffs = lastPatch.diffs;
        if (lastDiffs.empty() || lastDiffs.back().operation != DMP::EQUAL) {
            lastDiffs.push_back(typename DMP::Diff(DMP::EQUAL, nullPadding));
            lastPatch.length1 += paddingLength;
            lastPatch.length2 += paddingLength;
        } else if (paddingLength > (int)lastDiffs.back().text.length()) {
            typename DMP::Diff &lastDiff = lastDiffs.back();
            int extraLength = paddingLength - (int)lastDiff.text.length();
            lastDiff.text += nullPadding.substr(0, extraLength);
            lastPatch.length1 += extraLength;
            lastPatch.length2 += extraLength;
        }

        // Add padding to text
        text_str = nullPadding + text_str + nullPadding;

        // Split max patches
        dmp.patch_splitMax(patchesCopy);

        // Apply each patch
        int x = 0;
        int delta = 0;
        result.second.resize(patchesCopy.size());
        string_t text1, text2;

        for (typename DMP::Patches::const_iterator cur_patch = patchesCopy.begin();
             cur_patch != patchesCopy.end(); ++cur_patch) {
            int expected_loc = cur_patch->start2 + delta;
            text1 = DMP::diff_text1(cur_patch->diffs);
            int start_loc;
            int end_loc = -1;

            if ((int)text1.length() > dmp.Match_MaxBits) {
                start_loc = dmp.match_main(text_str,
                    text1.substr(0, dmp.Match_MaxBits), expected_loc);
                if (start_loc != -1) {
                    end_loc = dmp.match_main(text_str,
                        text1.substr(text1.length() - dmp.Match_MaxBits),
                        expected_loc + (int)text1.length() - dmp.Match_MaxBits);
                    if (end_loc == -1 || start_loc >= end_loc) {
                        start_loc = -1;
                    }
                }
            } else {
                start_loc = dmp.match_main(text_str, text1, expected_loc);
            }

            if (start_loc == -1) {
                result.second[x] = false;
                delta -= cur_patch->length2 - cur_patch->length1;
            } else {
                result.second[x] = true;
                delta = start_loc - expected_loc;
                if (end_loc == -1) {
                    text2 = text_str.substr(start_loc, text1.length());
                } else {
                    text2 = text_str.substr(start_loc,
                        end_loc + dmp.Match_MaxBits - start_loc);
                }
                if (text1 == text2) {
                    text_str = text_str.substr(0, start_loc)
                        + DMP::diff_text2(cur_patch->diffs)
                        + text_str.substr(start_loc + text1.length());
                } else {
                    typename DMP::Diffs diffs = dmp.diff_main(text1, text2, false);
                    if ((int)text1.length() > dmp.Match_MaxBits
                        && DMP::diff_levenshtein(diffs) / (float)text1.length()
                            > dmp.Patch_DeleteThreshold) {
                        result.second[x] = false;
                    } else {
                        DMP::diff_cleanupSemanticLossless(diffs);
                        int index1 = 0;
                        for (typename DMP::Diffs::const_iterator d = cur_patch->diffs.begin();
                             d != cur_patch->diffs.end(); ++d) {
                            if (d->operation != DMP::EQUAL) {
                                int index2 = DMP::diff_xIndex(diffs, index1);
                                if (d->operation == DMP::INSERT) {
                                    text_str = text_str.substr(0, start_loc + index2)
                                        + d->text + text_str.substr(start_loc + index2);
                                } else if (d->operation == DMP::DELETE) {
                                    text_str = text_str.substr(0, start_loc + index2)
                                        + text_str.substr(start_loc
                                            + DMP::diff_xIndex(diffs, index1 + (int)d->text.length()));
                                }
                            }
                            if (d->operation != DMP::DELETE) {
                                index1 += (int)d->text.length();
                            }
                        }
                    }
                }
            }
            x++;
        }

        // Strip the padding off
        result.first = text_str.substr(nullPadding.length(),
            text_str.length() - 2 * nullPadding.length());
    } else {
        result.first = text_str;
        result.second.clear();
    }

    Py_END_ALLOW_THREADS /* ACQUIRE THE GIL */

    PyObject *ret = PyTuple_New(2);
    PyTuple_SetItem(ret, 0, Shim::from_string(result.first));

    PyObject *bool_list = PyList_New(result.second.size());
    for (size_t i = 0; i < result.second.size(); i++) {
        PyList_SetItem(bool_list, i, PyBool_FromLong(result.second[i]));
    }
    PyTuple_SetItem(ret, 1, bool_list);

    return ret;
}

template <class Shim>
static PyObject *
diff_match_patch__patch_fromText__impl(PyObject *self, PyObject *args, PyObject *kwargs)
{
    typename Shim::PY_ARG_TYPE patch_text;

    static char *kwlist[] = {
        strdup("patch_text"),
        NULL };

    char format_spec[64];
    sprintf(format_spec, "%s", Shim::PyArgFormat);
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, format_spec, kwlist,
                                     &patch_text))
        return NULL;

    auto patch_str = Shim::to_string(patch_text);

    typedef diff_match_patch<typename Shim::STL_STRING_TYPE> DMP;
    DMP dmp;

    typename DMP::Patches patches;
    patches = dmp.patch_fromText(patch_str);

    PyObject *ret = PyList_New(0);

    PyObject *opcodes[3];
    opcodes[0] = PyUnicode_FromString("-");
    opcodes[1] = PyUnicode_FromString("+");
    opcodes[2] = PyUnicode_FromString("=");

    for (typename DMP::Patches::const_iterator p = patches.begin(); p != patches.end(); ++p) {
        PyObject *dict = PyDict_New();
        PyDict_SetItemString(dict, "start1", PyLong_FromLong(p->start1));
        PyDict_SetItemString(dict, "length1", PyLong_FromLong(p->length1));
        PyDict_SetItemString(dict, "start2", PyLong_FromLong(p->start2));
        PyDict_SetItemString(dict, "length2", PyLong_FromLong(p->length2));

        PyObject *diffs_list = PyList_New(0);
        for (typename DMP::Diffs::const_iterator d = p->diffs.begin(); d != p->diffs.end(); ++d) {
            PyObject *tuple = PyTuple_New(2);
            Py_INCREF(opcodes[d->operation]);
            PyTuple_SetItem(tuple, 0, opcodes[d->operation]);
            PyTuple_SetItem(tuple, 1, Shim::from_string(d->text));
            PyList_Append(diffs_list, tuple);
            Py_DECREF(tuple);
        }
        PyDict_SetItemString(dict, "diffs", diffs_list);
        Py_DECREF(diffs_list);

        PyList_Append(ret, dict);
        Py_DECREF(dict);
    }

    Py_DECREF(opcodes[0]);
    Py_DECREF(opcodes[1]);
    Py_DECREF(opcodes[2]);

    return ret;
}

template <class Shim>
static PyObject *
diff_match_patch__diff_lines__impl(PyObject *self, PyObject *args, PyObject *kwargs)
{
    typename Shim::PY_ARG_TYPE text1, text2;
    char format_spec[64];

    static char *kwlist[] = {
        strdup("text1"),
        strdup("text2"),
        NULL };

    sprintf(format_spec, "%s%s", Shim::PyArgFormat, Shim::PyArgFormat);
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, format_spec, kwlist,
                                     &text1, &text2))
        return NULL;

    auto text1_str = Shim::to_string(text1),
         text2_str = Shim::to_string(text2);

    typedef typename Shim::STL_STRING_TYPE string_t;
    typedef typename string_t::value_type char_t;
    typedef diff_match_patch<string_t> DMP;
    typedef DMPAccess<string_t> Access;

    // Step 1+2: equivalent to diff_linesToChars
    typename Access::Lines lines;
    Access::diff_linesToChars(text1_str, text2_str, lines);

    // Step 3: diff_main on encoded strings
    DMP dmp;
    typename DMP::Diffs diffs = dmp.diff_main(text1_str, text2_str, false);

    // Step 4: decode — equivalent to diff_charsToLines (which is private)
    for (typename DMP::Diffs::iterator d = diffs.begin(); d != diffs.end(); ++d) {
        string_t text;
        for (int y = 0; y < (int)d->text.length(); y++) {
            typedef typename std::make_unsigned<char_t>::type uchar_t;
            const auto& lp = lines[static_cast<size_t>(static_cast<uchar_t>(d->text[y]))];
            text.append(lp.first, lp.second);
        }
        d->text.swap(text);
    }

    // Convert to Python list of (op, text) tuples
    PyObject *ret = PyList_New(0);

    PyObject *opcodes[3];
    opcodes[0] = PyUnicode_FromString("-");
    opcodes[1] = PyUnicode_FromString("+");
    opcodes[2] = PyUnicode_FromString("=");

    for (typename DMP::Diffs::const_iterator d = diffs.begin(); d != diffs.end(); ++d) {
        typename DMP::Diff entry = *d;
        PyObject *tuple = PyTuple_New(2);
        Py_INCREF(opcodes[entry.operation]);
        PyTuple_SetItem(tuple, 0, opcodes[entry.operation]);
        PyTuple_SetItem(tuple, 1, Shim::from_string(entry.text));
        PyList_Append(ret, tuple);
        Py_DECREF(tuple);
    }

    Py_DECREF(opcodes[0]);
    Py_DECREF(opcodes[1]);
    Py_DECREF(opcodes[2]);

    return ret;
}

// WRAPPER FUNCTIONS THAT DETERMINE WHETHER UNICODE OR BYTES ARE PASSED

static PyObject *
diff_match_patch__diff(PyObject *self, PyObject *args, PyObject *kwargs)
{
    // Check if the first argument is a Unicode object, and if so, run
    // the Unicode version of the method. Otherwise run the bytes version.
    PyObject* first_arg;
    if (PyTuple_Size(args) > 0 && (first_arg = PyTuple_GetItem(args, 0)))
        if (PyUnicode_Check(first_arg))
            return diff_match_patch__diff__impl<UnicodeShim>(self, args, kwargs);
    return diff_match_patch__diff__impl<BytesShim>(self, args, kwargs);
}

static PyObject *
diff_match_patch__match(PyObject *self, PyObject *args, PyObject *kwargs)
{
    // Check if the first argument is a Unicode object, and if so, run
    // the Unicode version of the method. Otherwise run the bytes version.
    PyObject* first_arg;
    if (PyTuple_Size(args) > 0 && (first_arg = PyTuple_GetItem(args, 0)))
        if (PyUnicode_Check(first_arg))
            return diff_match_patch__match__impl<UnicodeShim>(self, args, kwargs);
    return diff_match_patch__match__impl<BytesShim>(self, args, kwargs);
}

static PyObject *
diff_match_patch__patch_apply(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject* first_arg;
    if (PyTuple_Size(args) > 0 && (first_arg = PyTuple_GetItem(args, 0)))
        if (PyUnicode_Check(first_arg))
            return diff_match_patch__patch_apply__impl<UnicodeShim>(self, args, kwargs);
    return diff_match_patch__patch_apply__impl<BytesShim>(self, args, kwargs);
}

static PyObject *
diff_match_patch__patch_fromText(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject* first_arg;
    if (PyTuple_Size(args) > 0 && (first_arg = PyTuple_GetItem(args, 0)))
        if (PyUnicode_Check(first_arg))
            return diff_match_patch__patch_fromText__impl<UnicodeShim>(self, args, kwargs);
    return diff_match_patch__patch_fromText__impl<BytesShim>(self, args, kwargs);
}

static PyObject *
diff_match_patch__diff_lines(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject* first_arg;
    if (PyTuple_Size(args) > 0 && (first_arg = PyTuple_GetItem(args, 0)))
        if (PyUnicode_Check(first_arg))
            return diff_match_patch__diff_lines__impl<UnicodeShim>(self, args, kwargs);
    return diff_match_patch__diff_lines__impl<BytesShim>(self, args, kwargs);
}

// EXTENSION MODULE METADATA

static PyMethodDef MyMethods[] = {
    {"diff", (PyCFunction)diff_match_patch__diff, METH_VARARGS|METH_KEYWORDS,
    "Compute the difference between two strings or bytes. Returns a list of tuples (OP, LEN)."},
    {"match", (PyCFunction)diff_match_patch__match, METH_VARARGS|METH_KEYWORDS,
    "Locate the best instance of 'pattern' in 'text' near 'loc'. Returns -1 if no match found."},
    {"patch_apply", (PyCFunction)diff_match_patch__patch_apply, METH_VARARGS|METH_KEYWORDS,
    "Apply a patch (in GNU diff format) to a text. Returns (patched_text, [applied_bools])."},
    {"patch_fromText", (PyCFunction)diff_match_patch__patch_fromText, METH_VARARGS|METH_KEYWORDS,
    "Parse a GNU diff format patch string. Returns a list of patch dicts with start1/2, length1/2, diffs."},
    {"diff_lines", (PyCFunction)diff_match_patch__diff_lines, METH_VARARGS|METH_KEYWORDS,
    "Diff two texts at line granularity. Returns a list of (OP, text) tuples."},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef mymodule = {
   PyModuleDef_HEAD_INIT,
   "fast_diff_match_patch",   /* name of module */
   NULL, /* module documentation, may be NULL */
   -1,       /* size of per-interpreter state of the module,
                or -1 if the module keeps state in global variables. */
   MyMethods
};

PyMODINIT_FUNC
PyInit_fast_diff_match_patch(void)
{
    auto module = PyModule_Create(&mymodule);
    PyModule_AddIntConstant(module, "CHAR_WIDTH", sizeof(UnicodeShim::STL_STRING_TYPE::value_type));
    return module;
}
