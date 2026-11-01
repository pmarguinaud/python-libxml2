#include "libxml_text.h"
#include "libxml_utils.h"
#include "libxml_document.h"

/* ------------------------------------------------------------------ */
/* CharacterData shared helpers                                        */
/* ------------------------------------------------------------------ */

static PyObject *
chardata_get_data(LibXML_NodeObject *self, void *closure)
{
    xmlChar *content = xmlNodeGetContent(self->node);
    if (content == NULL) return PyUnicode_FromString("");
    PyObject *ret = PyUnicode_FromString((const char *)content);
    xmlFree(content);
    return ret;
}

static int
chardata_set_data(LibXML_NodeObject *self, PyObject *value, void *closure)
{
    if (value == NULL || value == Py_None) {
        xmlNodeSetContent(self->node, BAD_CAST "");
        return 0;
    }
    const char *s = PyUnicode_AsUTF8(value);
    if (s == NULL) return -1;
    xmlNodeSetContent(self->node, BAD_CAST s);
    return 0;
}

static PyObject *
chardata_setData(LibXML_NodeObject *self, PyObject *args)
{
    const char *data;
    if (!PyArg_ParseTuple(args, "s", &data)) return NULL;
    xmlNodeSetContent(self->node, BAD_CAST data);
    Py_RETURN_NONE;
}

/* Offsets count characters, and content is UTF-8. Bytes taken by `chars`
 * characters at the start of s, up to the end of s. */
static int
char_bytes(const xmlChar *s, int chars)
{
    int i = 0;
    while (chars > 0 && s[i]) {
        i++;
        while ((s[i] & 0xC0) == 0x80) i++;
        chars--;
    }
    return i;
}

static PyObject *
chardata_substringData(LibXML_NodeObject *self, PyObject *args)
{
    int offset, length;
    if (!PyArg_ParseTuple(args, "ii", &offset, &length)) return NULL;
    if (offset < 0 || length < 0) Py_RETURN_NONE;

    xmlChar *content = xmlNodeGetContent(self->node);
    if (!content) return PyUnicode_FromString("");
    int start = char_bytes(content, offset);
    int size  = char_bytes(content + start, length);
    PyObject *ret = PyUnicode_FromStringAndSize((const char *)content + start, size);
    xmlFree(content);
    return ret;
}

static PyObject *
chardata_appendData(LibXML_NodeObject *self, PyObject *args)
{
    const char *data;
    if (!PyArg_ParseTuple(args, "s", &data)) return NULL;
    xmlNodeAddContent(self->node, BAD_CAST data);
    Py_RETURN_NONE;
}

/* Replace `drop` bytes of the content at byte `start`. Takes old. */
static PyObject *
splice_content(LibXML_NodeObject *self, xmlChar *old, int start, int drop,
               const char *data)
{
    int total = xmlStrlen(old);
    int dlen  = (int)strlen(data);

    xmlChar *newval = xmlMalloc(total - drop + dlen + 1);
    if (!newval) { xmlFree(old); return PyErr_NoMemory(); }
    memcpy(newval, old, start);
    memcpy(newval + start, data, dlen);
    memcpy(newval + start + dlen, old + start + drop, total - start - drop + 1);
    xmlNodeSetContent(self->node, newval);
    xmlFree(old);
    xmlFree(newval);
    Py_RETURN_NONE;
}

static PyObject *
chardata_insertData(LibXML_NodeObject *self, PyObject *args)
{
    int offset;
    const char *data;
    if (!PyArg_ParseTuple(args, "is", &offset, &data)) return NULL;
    if (offset < 0 || data[0] == '\0') Py_RETURN_NONE;

    xmlChar *old = xmlNodeGetContent(self->node);
    if (!old) old = xmlStrdup(BAD_CAST "");
    return splice_content(self, old, char_bytes(old, offset), 0, data);
}

static PyObject *
chardata_deleteData(LibXML_NodeObject *self, PyObject *args)
{
    int offset, length;
    if (!PyArg_ParseTuple(args, "ii", &offset, &length)) return NULL;
    if (offset < 0 || length <= 0) Py_RETURN_NONE;

    xmlChar *old = xmlNodeGetContent(self->node);
    if (!old) Py_RETURN_NONE;
    int start = char_bytes(old, offset);
    return splice_content(self, old, start, char_bytes(old + start, length), "");
}

/* Replace `from` with `to` in one pass over src, like Perl's s///: text that
 * was just inserted is never scanned again. Returns a new string, or NULL if
 * out of memory. `from` must not be empty. */
static xmlChar *
str_replace(const xmlChar *src, const char *from, const char *to, int all)
{
    size_t flen = strlen(from), tlen = strlen(to), count = 0;
    const xmlChar *p = src, *hit;

    while ((hit = xmlStrstr(p, BAD_CAST from)) != NULL) {
        count++;
        p = hit + flen;
        if (!all) break;
    }

    size_t total = strlen((const char *)src) - count * flen + count * tlen;
    xmlChar *out = xmlMalloc(total + 1);
    if (out == NULL) return NULL;

    xmlChar *w = out;
    p = src;
    for (size_t i = 0; i < count; i++) {
        hit = xmlStrstr(p, BAD_CAST from);
        memcpy(w, p, hit - p);
        w += hit - p;
        memcpy(w, to, tlen);
        w += tlen;
        p = hit + flen;
    }
    strcpy((char *)w, (const char *)p);
    return out;
}

/* Shared body of replaceDataString and deleteDataString */
static PyObject *
chardata_replace_string(LibXML_NodeObject *self, const char *from,
                        const char *to, int all)
{
    if (from[0] == '\0') Py_RETURN_NONE;

    xmlChar *content = xmlNodeGetContent(self->node);
    if (!content) Py_RETURN_NONE;

    xmlChar *newval = str_replace(content, from, to, all);
    xmlFree(content);
    if (!newval) return PyErr_NoMemory();

    xmlNodeSetContent(self->node, newval);
    xmlFree(newval);
    Py_RETURN_NONE;
}

static PyObject *
chardata_deleteDataString(LibXML_NodeObject *self, PyObject *args)
{
    const char *target;
    int all = 0;
    if (!PyArg_ParseTuple(args, "s|i", &target, &all)) return NULL;
    return chardata_replace_string(self, target, "", all);
}

static PyObject *
chardata_replaceData(LibXML_NodeObject *self, PyObject *args)
{
    int offset, length;
    const char *data;
    if (!PyArg_ParseTuple(args, "iis", &offset, &length, &data)) return NULL;
    if (offset < 0 || length < 0 || data[0] == '\0') Py_RETURN_NONE;

    xmlChar *old = xmlNodeGetContent(self->node);
    if (!old) Py_RETURN_NONE;

    int start = char_bytes(old, offset);
    if (old[start] == '\0') {   /* nothing at this offset, as in Perl */
        xmlFree(old);
        Py_RETURN_NONE;
    }
    return splice_content(self, old, start, char_bytes(old + start, length), data);
}

static PyObject *
chardata_replaceDataString(LibXML_NodeObject *self, PyObject *args)
{
    const char *old_str, *new_str;
    int all = 0;
    if (!PyArg_ParseTuple(args, "ss|i", &old_str, &new_str, &all)) return NULL;

    return chardata_replace_string(self, old_str, new_str, all);
}

static PyGetSetDef chardata_getset[] = {
    {"data", (getter)chardata_get_data, (setter)chardata_set_data, "Character data", NULL},
    {NULL}
};

static PyObject *
chardata_getData(LibXML_NodeObject *self, PyObject *args)
{
    return chardata_get_data(self, NULL);
}

static PyObject *
chardata_string_value(LibXML_NodeObject *self, PyObject *args)
{
    return chardata_get_data(self, NULL);
}

static PyMethodDef chardata_methods[] = {
    {"setData",           (PyCFunction)chardata_setData,           METH_VARARGS, "Set data"},
    {"getData",           (PyCFunction)chardata_getData,           METH_NOARGS,  "Get data"},
    {"substringData",     (PyCFunction)chardata_substringData,     METH_VARARGS, "Get substring"},
    {"appendData",        (PyCFunction)chardata_appendData,        METH_VARARGS, "Append data"},
    {"insertData",        (PyCFunction)chardata_insertData,        METH_VARARGS, "Insert data at offset"},
    {"deleteData",        (PyCFunction)chardata_deleteData,        METH_VARARGS, "Delete data range"},
    {"deleteDataString",  (PyCFunction)chardata_deleteDataString,  METH_VARARGS, "Delete first/all occurrences of string"},
    {"replaceData",       (PyCFunction)chardata_replaceData,       METH_VARARGS, "Replace data range"},
    {"replaceDataString", (PyCFunction)chardata_replaceDataString, METH_VARARGS, "Replace string occurrences"},
    {"string_value",      (PyCFunction)chardata_string_value,      METH_NOARGS,  "Return text content"},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* Text tp_new                                                         */
/* ------------------------------------------------------------------ */

/* Create a node in a hidden document. make_node gets the document. */
typedef xmlNodePtr (*make_node_fn)(xmlDocPtr doc, const char *a, const char *b);

static PyObject *
new_standalone(make_node_fn make_node, const char *a, const char *b)
{
    PyObject *doc = LibXML_new_hidden_doc();
    if (doc == NULL) return NULL;

    xmlNodePtr node = make_node((xmlDocPtr)LibXML_NODE(doc), a, b);
    PyObject *result = node ? LibXML_Node_Wrap(node) : PyErr_NoMemory();
    if (node && !result) xmlFreeNode(node);
    Py_DECREF(doc);
    return result;
}

static xmlNodePtr
make_text(xmlDocPtr doc, const char *a, const char *b)
{
    return xmlNewDocText(doc, BAD_CAST a);
}

static xmlNodePtr
make_comment(xmlDocPtr doc, const char *a, const char *b)
{
    return xmlNewDocComment(doc, BAD_CAST a);
}

static xmlNodePtr
make_cdata(xmlDocPtr doc, const char *a, const char *b)
{
    return xmlNewCDataBlock(doc, BAD_CAST a, (int)strlen(a));
}

static xmlNodePtr
make_pi(xmlDocPtr doc, const char *a, const char *b)
{
    return xmlNewDocPI(doc, BAD_CAST a, b ? BAD_CAST b : NULL);
}

static PyObject *
LibXML_Text_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *content = "";
    if (!PyArg_ParseTuple(args, "|s", &content)) return NULL;
    return new_standalone(make_text, content, NULL);
}

static PyObject *
LibXML_Comment_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *content = "";
    if (!PyArg_ParseTuple(args, "|s", &content)) return NULL;
    return new_standalone(make_comment, content, NULL);
}

static PyObject *
LibXML_CDATASection_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *content = "";
    if (!PyArg_ParseTuple(args, "|s", &content)) return NULL;
    return new_standalone(make_cdata, content, NULL);
}

static PyObject *
LibXML_PI_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *target, *data = NULL;
    if (!PyArg_ParseTuple(args, "s|s", &target, &data)) return NULL;
    return new_standalone(make_pi, target, data);
}

/* PI-specific methods */
static PyObject *
pi_get_target(LibXML_NodeObject *self, void *closure)
{
    if (!self->node->name) Py_RETURN_NONE;
    return PyUnicode_FromString((const char *)self->node->name);
}

static PyObject *
pi_get_data(LibXML_NodeObject *self, void *closure)
{
    return chardata_get_data(self, closure);
}

static int
pi_set_data(LibXML_NodeObject *self, PyObject *value, void *closure)
{
    return chardata_set_data(self, value, closure);
}

static PyObject *
pi_setData(LibXML_NodeObject *self, PyObject *args)
{
    return chardata_setData(self, args);
}

static PyGetSetDef pi_getset[] = {
    {"target", (getter)pi_get_target, NULL,           "PI target", NULL},
    {"data",   (getter)pi_get_data,   (setter)pi_set_data, "PI data", NULL},
    {NULL}
};

static PyObject *
pi_getData(LibXML_NodeObject *self, PyObject *args)
{
    return chardata_get_data(self, NULL);
}

static PyMethodDef pi_methods[] = {
    {"setData", (PyCFunction)pi_setData, METH_VARARGS, "Set PI data"},
    {"getData", (PyCFunction)pi_getData, METH_NOARGS,  "Get PI data"},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/* Type definitions                                                    */
/* ------------------------------------------------------------------ */

PyTypeObject LibXML_TextType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Text",
    .tp_basicsize = sizeof(LibXML_NodeObject),
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base      = &LibXML_NodeType,
    .tp_new       = LibXML_Text_new,
    .tp_methods   = chardata_methods,
    .tp_getset    = chardata_getset,
    .tp_doc       = "DOM Text node",
};

PyTypeObject LibXML_CommentType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.Comment",
    .tp_basicsize = sizeof(LibXML_NodeObject),
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base      = &LibXML_NodeType,
    .tp_new       = LibXML_Comment_new,
    .tp_methods   = chardata_methods,
    .tp_getset    = chardata_getset,
    .tp_doc       = "DOM Comment node",
};

PyTypeObject LibXML_CDATASectionType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.CDATASection",
    .tp_basicsize = sizeof(LibXML_NodeObject),
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base      = &LibXML_NodeType,
    .tp_new       = LibXML_CDATASection_new,
    .tp_methods   = chardata_methods,
    .tp_getset    = chardata_getset,
    .tp_doc       = "DOM CDATASection node",
};

PyTypeObject LibXML_PIType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "xml_libxml.PI",
    .tp_basicsize = sizeof(LibXML_NodeObject),
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base      = &LibXML_NodeType,
    .tp_new       = LibXML_PI_new,
    .tp_methods   = pi_methods,
    .tp_getset    = pi_getset,
    .tp_doc       = "DOM ProcessingInstruction node",
};
