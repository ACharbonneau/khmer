//
// This file is part of khmer, http://github.com/ged-lab/khmer/, and is
// Copyright (C) Michigan State University, 2009-2015. It is licensed under
// the three-clause BSD license; see doc/LICENSE.txt.
// Contact: khmer-project@idyll.org
//

//
// A module for Python that exports khmer C++ library functions.
//

// Must be first.
#include <Python.h>

#include <iostream>

#include "khmer.hh"
#include "kmer_hash.hh"
#include "hashtable.hh"
#include "hashbits.hh"
#include "counting.hh"
#include "read_aligner.hh"
#include "labelhash.hh"
#include "khmer_exception.hh"
#include "hllcounter.hh"

//
// Python 2/3 compatibility: PyInt and PyLong
//

#if (PY_MAJOR_VERSION >= 3)
    #define PyInt_Check(arg) PyLong_Check(arg)
    #define PyInt_AsLong(arg) PyLong_AsLong(arg)
#endif

//
// Python 2.6 compatibility macros: PyCapsule and PyCObject
// https://docs.python.org/2/howto/cporting.html#cobject-replaced-with-capsule
//

#include "capsulethunk.h"

//
// Python 2/3 compatibility: PyBytes and PyString
// https://docs.python.org/2/howto/cporting.html#str-unicode-unification
//

#include "bytesobject.h"

using namespace khmer;
using namespace read_parsers;

//
// Function necessary for Python loading:
//

extern "C" {
    void init_khmer();
}

// Configure module logging.
//#define WITH_INTERNAL_TRACING
namespace khmer
{

namespace python
{

#ifdef WITH_INTERNAL_TRACING
#warning "Internal tracing of Python extension module is enabled."
static uint8_t const    _MODULE_TRACE_LEVEL = TraceLogger:: TLVL_DEBUG9;
static void     _trace_logger(
    uint8_t level, char const * format, ...
)
{
    static FILE *   _stream_handle  = NULL;

    if (NULL == _stream_handle) {
        _stream_handle = fopen( "pymod.log", "w" );
    }

    va_list varargs;

    if (_MODULE_TRACE_LEVEL <= level) {
        va_start( varargs, format );
        vfprintf( _stream_handle, format, varargs );
        va_end( varargs );
        fflush( _stream_handle );
    }

}
#endif


static inline
void
_debug_class_attrs( PyTypeObject &tobj )
{
#ifdef WITH_INTERNAL_TRACING
    PyObject *key, *val;
    Py_ssize_t pos = 0;

    while (PyDict_Next( tobj.tp_dict, &pos, &key, &val )) {
        _trace_logger(
            TraceLogger:: TLVL_DEBUG5,
            "\ttype '%s' dictionary key %d: '%s'\n",
            tobj.tp_name, pos, PyBytes_AsString( key )
        );
    }
#endif // WITH_INTERNAL_TRACING
}


} // namespace python

} // namespace khmer


class _khmer_exception
{
private:
    std::string _message;
public:
    _khmer_exception(std::string message) : _message(message) { };
    inline const std::string get_message() const
    {
        return _message;
    };
};

class _khmer_signal : public _khmer_exception
{
public:
    _khmer_signal(std::string message) : _khmer_exception(message) { };
};

typedef pre_partition_info _pre_partition_info;

// default callback obj;
static PyObject *_callback_obj = NULL;

// callback function to pass into C++ functions

void _report_fn(const char * info, void * data, unsigned long long n_reads,
                unsigned long long other)
{
    // handle signals etc. (like CTRL-C)
    if (PyErr_CheckSignals() != 0) {
        throw _khmer_signal("PyErr_CheckSignals received a signal");
    }

    // set data to default?
    if (!data && _callback_obj) {
        data = _callback_obj;
    }

    // if 'data' is set, it is None, or a Python callable
    if (data) {
        PyObject * obj = (PyObject *) data;
        if (obj != Py_None) {
            PyObject * args = Py_BuildValue("sKK", info, n_reads, other);
            if (args != NULL) {
                PyObject * r = PyObject_Call(obj, args, NULL);
                Py_XDECREF(r);
            }
            Py_XDECREF(args);
        }
    }

    if (PyErr_Occurred()) {
        throw _khmer_signal("PyErr_Occurred is set");
    }

    // ...allow other Python threads to do stuff...
    Py_BEGIN_ALLOW_THREADS;
    Py_END_ALLOW_THREADS;
}

/***********************************************************************/

//
// Read object -- name, sequence, and FASTQ stuff
//

namespace khmer
{

namespace python
{

typedef struct {
    PyObject_HEAD
    //! Pointer to the low-level genomic read object.
    read_parsers:: Read *   read;
} khmer_Read_Object;


static
void
khmer_Read_dealloc(khmer_Read_Object * obj)
{
    delete obj->read;
    obj->read = NULL;
    Py_TYPE(obj)->tp_free((PyObject*)obj);
}


static
PyObject *
Read_get_name(khmer_Read_Object * obj, void * closure )
{
    return PyBytes_FromString(obj->read->name.c_str()) ;
}


static
PyObject *
Read_get_sequence(khmer_Read_Object * obj, void * closure)
{
    return PyBytes_FromString(obj->read->sequence.c_str()) ;
}


static
PyObject *
Read_get_accuracy(khmer_Read_Object * obj, void * closure)
{
    return PyBytes_FromString(obj->read->accuracy.c_str()) ;
}


static
PyObject *
Read_get_annotations(khmer_Read_Object * obj, void * closure)
{
    return PyBytes_FromString(obj->read->annotations.c_str()) ;
}


// TODO? Implement setters.


static PyGetSetDef khmer_Read_accessors [ ] = {
    {
        (char *)"name",
        (getter)Read_get_name, (setter)NULL,
        (char *)"Read identifier.", NULL
    },
    {
        (char *)"sequence",
        (getter)Read_get_sequence, (setter)NULL,
        (char *)"Genomic sequence.", NULL
    },
    {
        (char *)"accuracy",
        (getter)Read_get_accuracy, (setter)NULL,
        (char *)"Quality scores.", NULL
    },
    {
        (char *)"annotations",
        (getter)Read_get_annotations, (setter)NULL,
        (char *)"Annotations.", NULL
    },

    { NULL, NULL, NULL, NULL, NULL } // sentinel
};


static PyTypeObject khmer_Read_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)        /* init & ob_size */
    "khmer.Read",                         /* tp_name */
    sizeof(khmer_Read_Object),            /* tp_basicsize */
    0,                                    /* tp_itemsize */
    (destructor)khmer_Read_dealloc,       /* tp_dealloc */
    0,                                    /* tp_print */
    0,                                    /* tp_getattr */
    0,                                    /* tp_setattr */
    0,                                    /* tp_compare */
    0,                                    /* tp_repr */
    0,                                    /* tp_as_number */
    0,                                    /* tp_as_sequence */
    0,                                    /* tp_as_mapping */
    0,                                    /* tp_hash */
    0,                                    /* tp_call */
    0,                                    /* tp_str */
    0,                                    /* tp_getattro */
    0,                                    /* tp_setattro */
    0,                                    /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                   /* tp_flags */
    "A FASTQ record plus some metadata.", /* tp_doc */
    0,                                    /* tp_traverse */
    0,                                    /* tp_clear */
    0,                                    /* tp_richcompare */
    0,                                    /* tp_weaklistoffset */
    0,                                    /* tp_iter */
    0,                                    /* tp_iternext */
    0,                                    /* tp_methods */
    0,                                    /* tp_members */
    (PyGetSetDef *)khmer_Read_accessors,  /* tp_getset */
};

/***********************************************************************/

//
// ReadParser object -- parse reads directly from streams
// ReadPairIterator -- return pairs of Read objects
//


typedef struct {
    PyObject_HEAD
    //! Pointer to the low-level parser object.
    read_parsers:: IParser *  parser;
} khmer_ReadParser_Object;


typedef struct {
    PyObject_HEAD
    //! Pointer to Python parser object for reference counting purposes.
    PyObject *  parent;
    //! Persistent value of pair mode across invocations.
    int pair_mode;
} khmer_ReadPairIterator_Object;


static
void
_ReadParser_dealloc(khmer_ReadParser_Object * obj)
{
    Py_DECREF(obj->parser);
    obj->parser = NULL;
    Py_TYPE(obj)->tp_free((PyObject*)obj);
}


static
void
khmer_ReadPairIterator_dealloc(khmer_ReadPairIterator_Object * obj)
{
    Py_DECREF(obj->parent);
    obj->parent = NULL;
    Py_TYPE(obj)->tp_free((PyObject*)obj);
}


static
PyObject *
_ReadParser_new( PyTypeObject * subtype, PyObject * args, PyObject * kwds )
{
    const char *      ifile_name_CSTR;

    if (!PyArg_ParseTuple(args, "s", &ifile_name_CSTR )) {
        return NULL;
    }
    std:: string    ifile_name( ifile_name_CSTR );

    PyObject * self     = subtype->tp_alloc( subtype, 1 );
    if (self == NULL) {
        return NULL;
    }
    khmer_ReadParser_Object * myself  = (khmer_ReadParser_Object *)self;

    // Wrap the low-level parser object.
    try {
        myself->parser =
            IParser:: get_parser( ifile_name );
    } catch (InvalidStreamHandle &exc) {
        PyErr_SetString( PyExc_ValueError, exc.what() );
        return NULL;
    }
    return self;
}


static
PyObject *
_ReadParser_iternext( PyObject * self )
{
    khmer_ReadParser_Object * myself  = (khmer_ReadParser_Object *)self;
    IParser *       parser  = myself->parser;

    bool    stop_iteration = false;
    char    const * exc = NULL;
    Read *  the_read_PTR    = new Read( );

    Py_BEGIN_ALLOW_THREADS
    stop_iteration = parser->is_complete( );
    if (!stop_iteration) {
        try {
            parser->imprint_next_read( *the_read_PTR );
        } catch (NoMoreReadsAvailable &e) {
            stop_iteration = true;
        } catch (StreamReadError &e) {
            exc = e.what();
        }
    }
    Py_END_ALLOW_THREADS

    // Note: Can simply return NULL instead of setting the StopIteration
    //       exception.
    if (stop_iteration) {
        delete the_read_PTR;
        return NULL;
    }

    if (exc != NULL) {
        delete the_read_PTR;
        PyErr_SetString(PyExc_IOError, exc);
        return NULL;
    }

    PyObject * the_read_OBJECT = khmer_Read_Type.tp_alloc( &khmer_Read_Type, 1 );
    ((khmer_Read_Object *)the_read_OBJECT)->read = the_read_PTR;
    return the_read_OBJECT;
}


static
PyObject *
_ReadPairIterator_iternext(khmer_ReadPairIterator_Object * myself)
{
    khmer_ReadParser_Object * parent = (khmer_ReadParser_Object*)myself->parent;
    IParser *           parser    = parent->parser;
    uint8_t         pair_mode = myself->pair_mode;

    ReadPair    the_read_pair;
    bool    stop_iteration      = false;
    bool    unknown_pair_reading_mode   = false;
    bool    invalid_read_pair       = false;
    bool    stream_read_error = false;
    Py_BEGIN_ALLOW_THREADS
    stop_iteration = parser->is_complete( );
    if (!stop_iteration)
        try {
            parser->imprint_next_read_pair( the_read_pair, pair_mode );
        } catch (UnknownPairReadingMode &exc) {
            unknown_pair_reading_mode = true;
        } catch (InvalidReadPair &exc) {
            invalid_read_pair = true;
        } catch (StreamReadError &exc) {
            stream_read_error = true;
        } catch (NoMoreReadsAvailable &exc) {
            stop_iteration = true;
        }
    Py_END_ALLOW_THREADS

    // Note: Can return NULL instead of setting the StopIteration exception.
    if (stop_iteration) {
        return NULL;
    }

    if (unknown_pair_reading_mode) {
        PyErr_SetString(
            PyExc_ValueError, "Unknown pair reading mode supplied."
        );
        return NULL;
    }
    if (invalid_read_pair) {
        PyErr_SetString( PyExc_IOError, "Invalid read pair detected." );
        return NULL;
    }

    if (stream_read_error) {
        PyErr_SetString( PyExc_IOError, "Input file error.");
        return NULL;
    }

    // Copy elements of 'ReadPair' object into Python tuple.
    // TODO? Replace dummy reads with 'None' object.
    PyObject * read_1_OBJECT = khmer_Read_Type.tp_alloc( &khmer_Read_Type, 1 );
    ((khmer_Read_Object *)read_1_OBJECT)->read = new Read( the_read_pair.first );
    PyObject * read_2_OBJECT = khmer_Read_Type.tp_alloc( &khmer_Read_Type, 1 );
    ((khmer_Read_Object *)read_2_OBJECT)->read = new Read( the_read_pair.second );
    PyObject * tup = PyTuple_Pack( 2, read_1_OBJECT, read_2_OBJECT );
    Py_XDECREF(read_1_OBJECT);
    Py_XDECREF(read_2_OBJECT);
    return tup;
}

static PyTypeObject khmer_ReadPairIterator_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "khmer.ReadPairIterator",                       /* tp_name */
    sizeof(khmer_ReadPairIterator_Object),          /* tp_basicsize */
    0,                                  /* tp_itemsize */
    (destructor)khmer_ReadPairIterator_dealloc,      /* tp_dealloc */
    0,                                         /* tp_print */
    0,                                         /* tp_getattr */
    0,                                         /* tp_setattr */
    0,                                         /* tp_compare */
    0,                                         /* tp_repr */
    0,                                         /* tp_as_number */
    0,                                         /* tp_as_sequence */
    0,                                         /* tp_as_mapping */
    0,                                         /* tp_hash */
    0,                                         /* tp_call */
    0,                                         /* tp_str */
    0,                                         /* tp_getattro */
    0,                                         /* tp_setattro */
    0,                                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,  /* tp_flags */
    "Iterates over 'ReadParser' objects and returns read pairs.",      /* tp_doc */
    0,                                         /* tp_traverse */
    0,                                         /* tp_clear */
    0,                                         /* tp_richcompare */
    0,                                         /* tp_weaklistoffset */
    PyObject_SelfIter,                                         /* tp_iter */
    (iternextfunc)_ReadPairIterator_iternext,                                         /* tp_iternext */
};



static
PyObject *
ReadParser_iter_reads(PyObject * self, PyObject * args )
{
    return PyObject_SelfIter( self );
}


static
PyObject *
ReadParser_iter_read_pairs(PyObject * self, PyObject * args )
{
    int  pair_mode  = IParser:: PAIR_MODE_ERROR_ON_UNPAIRED;

    if (!PyArg_ParseTuple( args, "|i", &pair_mode )) {
        return NULL;
    }

    // Capture existing read parser.
    PyObject * obj = khmer_ReadPairIterator_Type.tp_alloc(
                         &khmer_ReadPairIterator_Type, 1
                     );
    if (obj == NULL) {
        return NULL;
    }
    khmer_ReadPairIterator_Object * rpi   = (khmer_ReadPairIterator_Object *)obj;
    rpi->parent             = self;
    rpi->pair_mode          = pair_mode;

    // Increment reference count on existing ReadParser object so that it
    // will not go away until all ReadPairIterator instances have gone away.
    Py_INCREF( self );

    return obj;
}


static PyMethodDef _ReadParser_methods [ ] = {
    {
        "iter_reads",       (PyCFunction)ReadParser_iter_reads,
        METH_NOARGS,        "Iterates over reads."
    },
    {
        "iter_read_pairs",  (PyCFunction)ReadParser_iter_read_pairs,
        METH_VARARGS,       "Iterates over paired reads as pairs."
    },

    { NULL, NULL, 0, NULL } // sentinel
};


static PyTypeObject khmer_ReadParser_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)             /* init & ob_size */
    "khmer.ReadParser",                        /* tp_name */
    sizeof(khmer_ReadParser_Object),           /* tp_basicsize */
    0,                                         /* tp_itemsize */
    (destructor)_ReadParser_dealloc,           /* tp_dealloc */
    0,                                         /* tp_print */
    0,                                         /* tp_getattr */
    0,                                         /* tp_setattr */
    0,                                         /* tp_compare */
    0,                                         /* tp_repr */
    0,                                         /* tp_as_number */
    0,                                         /* tp_as_sequence */
    0,                                         /* tp_as_mapping */
    0,                                         /* tp_hash */
    0,                                         /* tp_call */
    0,                                         /* tp_str */
    0,                                         /* tp_getattro */
    0,                                         /* tp_setattro */
    0,                                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                        /* tp_flags */
    "Parses streams from various file formats, " \
    "such as FASTA and FASTQ.",                /* tp_doc */
    0,                                         /* tp_traverse */
    0,                                         /* tp_clear */
    0,                                         /* tp_richcompare */
    0,                                         /* tp_weaklistoffset */
    PyObject_SelfIter,                         /* tp_iter */
    (iternextfunc)_ReadParser_iternext,        /* tp_iternext */
    _ReadParser_methods,                       /* tp_methods */
    0,                                         /* tp_members */
    0,                                         /* tp_getset */
    0,                                         /* tp_base */
    0,                                         /* tp_dict */
    0,                                         /* tp_descr_get */
    0,                                         /* tp_descr_set */
    0,                                         /* tp_dictoffset */
    0,                                         /* tp_init */
    0,                                         /* tp_alloc */
    _ReadParser_new,                           /* tp_new */
};

/*
    PyObject * cls_attrs_DICT = PyDict_New( );
    if (cls_attrs_DICT == NULL) {
        return;
    }

    // Place pair mode constants into class dictionary.
    int result;

    PyObject * value = PyLong_FromLong( IParser:: PAIR_MODE_ALLOW_UNPAIRED );
    result = PyDict_SetItemString(cls_attrs_DICT,
                                  "PAIR_MODE_ALLOW_UNPAIRED", value);
    Py_XDECREF(value);
    if (!result) {
        Py_DECREF(cls_attrs_DICT);
        return;
    }

    value = PyLong_FromLong( IParser:: PAIR_MODE_IGNORE_UNPAIRED );
    result = PyDict_SetItemString(cls_attrs_DICT,
                                  "PAIR_MODE_IGNORE_UNPAIRED", value );
    Py_XDECREF(value);
    if (!result) {
        Py_DECREF(cls_attrs_DICT);
        return;
    }

    value = PyLong_FromLong( IParser:: PAIR_MODE_ERROR_ON_UNPAIRED );
    result = PyDict_SetItemString(cls_attrs_DICT,
                                  "PAIR_MODE_ERROR_ON_UNPAIRED", value);
    Py_XDECREF(value);
    if (!result) {
        Py_DECREF(cls_attrs_DICT);
        return;
    }

    ReadParser_Type.tp_dict     = cls_attrs_DICT;
*/

} // namespace python

} // namespace khmer


static
read_parsers:: IParser *
_PyObject_to_khmer_ReadParser( PyObject * py_object )
{
    // TODO: Add type-checking.

    return ((python:: khmer_ReadParser_Object *)py_object)->parser;
}


/***********************************************************************/

//
// KCountingHash object
//

void free_pre_partition_info(PyObject* tm)
{
    void * p = PyCapsule_GetPointer(tm, "khmer.pre_partition_info");
    _pre_partition_info * ppi = (_pre_partition_info *) p;
    delete ppi;
}

void free_subset_partition_info(PyObject* tm)
{
    void * p = PyCapsule_GetPointer(tm, "khmer.SubsetPartition");
    SubsetPartition * subset_p = (SubsetPartition *) p;
    delete subset_p;
}

typedef struct {
    PyObject_HEAD
    CountingHash * counting;
} khmer_KCountingHashObject;

typedef struct {
    PyObject_HEAD
    SubsetPartition * subset;
} khmer_KSubsetPartitionObject;

typedef struct {
    PyObject_HEAD
    Hashbits * hashbits;
} khmer_KHashbitsObject;

static void khmer_subset_dealloc(PyObject *);

static PyTypeObject khmer_KSubsetPartitionType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "KSubset", sizeof(khmer_KSubsetPartitionObject),
    0,
    khmer_subset_dealloc,   /*tp_dealloc*/
    0,              /*tp_print*/
    0,              /*tp_getattr*/
    0,              /*tp_setattr*/
    0,              /*tp_compare*/
    0,              /*tp_repr*/
    0,              /*tp_as_number*/
    0,              /*tp_as_sequence*/
    0,              /*tp_as_mapping*/
    0,              /*tp_hash */
    0,              /*tp_call*/
    0,              /*tp_str*/
    0,              /*tp_getattro*/
    0,              /*tp_setattro*/
    0,              /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,     /*tp_flags*/
    "subset object",           /* tp_doc */
};

typedef struct {
    PyObject_HEAD
    ReadAligner * aligner;
} khmer_ReadAligner_Object;

static void khmer_counting_dealloc(PyObject *);

static PyObject * hash_abundance_distribution(PyObject * self,
        PyObject * args);

static PyObject * hash_abundance_distribution_with_reads_parser(
    PyObject * self,
    PyObject * args);

static PyObject * hash_set_use_bigcount(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    PyObject * x;
    if (!PyArg_ParseTuple(args, "O", &x)) {
        return NULL;
    }
    int setme = PyObject_IsTrue(x);
    if (setme < 0) {
        return NULL;
    }
    counting->set_use_bigcount((bool)setme);

    Py_RETURN_NONE;
}

static PyObject * hash_get_use_bigcount(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    bool val = counting->get_use_bigcount();

    return PyBool_FromLong((int)val);
}

static PyObject * hash_n_occupied(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    HashIntoType start = 0, stop = 0;

    if (!PyArg_ParseTuple(args, "|KK", &start, &stop)) {
        return NULL;
    }

    HashIntoType n = counting->n_occupied(start, stop);

    return PyLong_FromUnsignedLongLong(n);
}

static PyObject * hash_n_unique_kmers(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    HashIntoType n = counting->n_unique_kmers();

    return PyLong_FromUnsignedLongLong(n);
}

static PyObject * hash_n_entries(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    return PyLong_FromUnsignedLongLong(counting->n_entries());
}

static PyObject * hash_count(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * kmer;

    if (!PyArg_ParseTuple(args, "s", &kmer)) {
        return NULL;
    }

    if (strlen(kmer) != counting->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "k-mer length must be the same as the hashtable k-size");
        return NULL;
    }

    counting->count(kmer);

    return PyLong_FromLong(1);
}

static PyObject * hash_output_fasta_kmer_pos_freq(PyObject * self,
        PyObject *args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * infile;
    const char * outfile;

    if (!PyArg_ParseTuple(args, "ss", &infile, &outfile)) {
        return NULL;
    }

    counting->output_fasta_kmer_pos_freq(infile, outfile);

    return PyLong_FromLong(0);
}

static PyObject * hash_consume_fasta(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me  = (khmer_KCountingHashObject *) self;
    CountingHash * counting  = me->counting;

    const char * filename;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(
                args, "s|O", &filename, &callback_obj
            )) {
        return NULL;
    }

    // call the C++ function, and trap signals => Python
    unsigned long long  n_consumed    = 0;
    unsigned int          total_reads   = 0;
    try {
        counting->consume_fasta(filename, total_reads, n_consumed,
                                _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        PyErr_SetString(PyExc_IOError, e.get_message().c_str());
        return NULL;
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    return Py_BuildValue("IK", total_reads, n_consumed);
}

static PyObject * hash_consume_fasta_with_reads_parser(
    PyObject * self, PyObject * args
)
{
    khmer_KCountingHashObject * me  = (khmer_KCountingHashObject *) self;
    CountingHash * counting  = me->counting;

    PyObject * rparser_obj = NULL;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(
                args, "O|O", &rparser_obj, &callback_obj
            )) {
        return NULL;
    }

    read_parsers:: IParser * rparser =
        _PyObject_to_khmer_ReadParser( rparser_obj );

    char const * exc = "";
    // call the C++ function, and trap signals => Python
    unsigned long long  n_consumed  = 0;
    unsigned int    total_reads = 0;
    bool        exc_raised  = false;
    Py_BEGIN_ALLOW_THREADS
    try {
        counting->consume_fasta(rparser, total_reads, n_consumed,
                                _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        exc = e.get_message().c_str();
        exc_raised = true;
    }
    Py_END_ALLOW_THREADS
    if (exc_raised) {
        PyErr_SetString(PyExc_IOError, exc);
        return NULL;
    }

    return Py_BuildValue("IK", total_reads, n_consumed);
}

static PyObject * hash_consume(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * long_str;

    if (!PyArg_ParseTuple(args, "s", &long_str)) {
        return NULL;
    }

    if (strlen(long_str) < counting->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "string length must >= the hashtable k-mer size");
        return NULL;
    }

    unsigned int n_consumed;
    n_consumed = counting->consume_string(long_str);

    return PyLong_FromLong(n_consumed);
}

static PyObject * hash_get_min_count(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * long_str;

    if (!PyArg_ParseTuple(args, "s", &long_str)) {
        return NULL;
    }

    if (strlen(long_str) < counting->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "string length must >= the hashtable k-mer size");
        return NULL;
    }

    BoundedCounterType c = counting->get_min_count(long_str);
    unsigned int N = c;

    return PyLong_FromLong(N);
}

static PyObject * hash_get_max_count(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * long_str;

    if (!PyArg_ParseTuple(args, "s", &long_str)) {
        return NULL;
    }

    if (strlen(long_str) < counting->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "string length must >= the hashtable k-mer size");
        return NULL;
    }

    BoundedCounterType c = counting->get_max_count(long_str);
    unsigned int N = c;

    return PyLong_FromLong(N);
}

static PyObject * hash_get_median_count(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * long_str;

    if (!PyArg_ParseTuple(args, "s", &long_str)) {
        return NULL;
    }

    if (strlen(long_str) < counting->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "string length must >= the hashtable k-mer size");
        return NULL;
    }

    BoundedCounterType med = 0;
    float average = 0, stddev = 0;

    counting->get_median_count(long_str, med, average, stddev);

    return Py_BuildValue("iff", med, average, stddev);
}

static PyObject * hash_get_kadian_count(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * long_str;
    unsigned int nk = 1;

    if (!PyArg_ParseTuple(args, "s|I", &long_str, &nk)) {
        return NULL;
    }

    if (strlen(long_str) < counting->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "string length must >= the hashtable k-mer size");
        return NULL;
    }

    BoundedCounterType kad = 0;

    counting->get_kadian_count(long_str, kad, nk);

    return Py_BuildValue("i", kad);
}

static PyObject * hash_get(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    PyObject * arg;

    if (!PyArg_ParseTuple(args, "O", &arg)) {
        return NULL;
    }

    unsigned long count = 0;

    if (PyInt_Check(arg)) {
        long pos = PyInt_AsLong(arg);
        count = counting->get_count((unsigned int) pos);
    } else if (PyBytes_Check(arg)) {
        std::string s = PyBytes_AsString(arg);

        if (strlen(s.c_str()) != counting->ksize()) {
            PyErr_SetString(PyExc_ValueError,
                            "k-mer size must equal the counting table k-mer size");
            return NULL;
        }

        count = counting->get_count(s.c_str());
    }

    return PyLong_FromLong(count);
}

static PyObject * count_trim_on_abundance(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * seq = NULL;
    unsigned int min_count_i = 0;

    if (!PyArg_ParseTuple(args, "sI", &seq, &min_count_i)) {
        return NULL;
    }

    unsigned long trim_at;
    Py_BEGIN_ALLOW_THREADS

    BoundedCounterType min_count = min_count_i;

    trim_at = counting->trim_on_abundance(seq, min_count);

    Py_END_ALLOW_THREADS;

    PyObject * trim_seq = PyBytes_FromStringAndSize(seq, trim_at);
    if (trim_seq == NULL) {
        return NULL;
    }
    PyObject * ret = Py_BuildValue("Ok", trim_seq, trim_at);
    Py_DECREF(trim_seq);

    return ret;
}
static PyObject * count_trim_below_abundance(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * seq = NULL;
    BoundedCounterType max_count_i = 0;

    if (!PyArg_ParseTuple(args, "sH", &seq, &max_count_i)) {
        return NULL;
    }

    unsigned long trim_at;
    Py_BEGIN_ALLOW_THREADS

    BoundedCounterType max_count = max_count_i;

    trim_at = counting->trim_below_abundance(seq, max_count);

    Py_END_ALLOW_THREADS;

    PyObject * trim_seq = PyBytes_FromStringAndSize(seq, trim_at);
    if (trim_seq == NULL) {
        return NULL;
    }
    PyObject * ret = Py_BuildValue("Ok", trim_seq, trim_at);
    Py_DECREF(trim_seq);

    return ret;
}

static PyObject * count_find_spectral_error_positions(PyObject * self,
        PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    khmer::CountingHash * counting = me->counting;

    char * seq = NULL;
    khmer::BoundedCounterType max_count = 0; // unsigned short int

    if (!PyArg_ParseTuple(args, "sH", &seq, &max_count)) {
        return NULL;
    }

    std::vector<unsigned int> posns;

    try {
        posns = counting->find_spectral_error_positions(seq, max_count);
    } catch (khmer_exception &e) {
        PyErr_SetString(PyExc_ValueError, e.what());
        return NULL;
    }

    Py_ssize_t posns_size = posns.size();

    PyObject * x = PyList_New(posns_size);
    if (x == NULL) {
        return NULL;
    }
    for (Py_ssize_t i = 0; i < posns_size; i++) {
        PyList_SET_ITEM(x, i, PyLong_FromLong(posns[i]));
    }

    return x;
}

static PyObject * hash_fasta_count_kmers_by_position(PyObject * self,
        PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * inputfile;
    unsigned int max_read_len = 0;
    long max_read_len_long;
    int limit_by_count_int;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(args, "sli|O", &inputfile, &max_read_len_long,
                          &limit_by_count_int, &callback_obj)) {
        return NULL;
    }
    if (max_read_len_long < 0 || max_read_len_long >= pow(2, 32)) {
        PyErr_SetString(
            PyExc_ValueError,
            "The 2nd argument must be positive and less than 2^32");
        return NULL;
    }
    if (limit_by_count_int < 0 || limit_by_count_int >= pow(2, 16)) {
        PyErr_SetString(
            PyExc_ValueError,
            "The 3rd argument must be positive and less than 2^16");
        return NULL;
    }
    max_read_len = (unsigned int) max_read_len_long;

    unsigned long long * counts;
    counts = counting->fasta_count_kmers_by_position(inputfile, max_read_len,
             (unsigned short) limit_by_count_int, _report_fn, callback_obj);

    PyObject * x = PyList_New(max_read_len);
    if (x == NULL) {
        delete[] counts;
        return NULL;
    }

    for (unsigned int i = 0; i < max_read_len; i++) {
        int ret = PyList_SetItem(x, i, PyLong_FromUnsignedLongLong(counts[i]));
        if (ret < 0) {
            delete[] counts;
            return NULL;
        }
    }

    delete[] counts;

    return x;
}

static PyObject * hash_fasta_dump_kmers_by_abundance(PyObject * self,
        PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * inputfile;
    int limit_by = 0;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(args, "si|O", &inputfile, &limit_by,
                          &callback_obj)) {
        return NULL;
    }

    counting->fasta_dump_kmers_by_abundance(inputfile,
                                            limit_by,
                                            _report_fn, callback_obj);


    Py_RETURN_NONE;
}

static PyObject * hash_load(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * filename = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    try {
        counting->load(filename);
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject * hash_save(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * filename = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    counting->save(filename);

    Py_RETURN_NONE;
}

static PyObject * hash_get_ksize(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    unsigned int k = counting->ksize();

    return PyLong_FromLong(k);
}

static PyObject * hash_get_hashsizes(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;


    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    std::vector<HashIntoType> ts = counting->get_tablesizes();

    PyObject * x = PyList_New(ts.size());
    for (size_t i = 0; i < ts.size(); i++) {
        PyList_SET_ITEM(x, i, PyLong_FromUnsignedLongLong(ts[i]));
    }

    return x;
}

static PyObject * hash_collect_high_abundance_kmers(PyObject * self,
        PyObject * args);

static PyObject * hash_consume_and_tag(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * seq;

    if (!PyArg_ParseTuple(args, "s", &seq)) {
        return NULL;
    }

    // call the C++ function, and trap signals => Python

    unsigned long long n_consumed = 0;
    try {
        // @CTB needs to normalize
        counting->consume_sequence_and_tag(seq, n_consumed);
    } catch (_khmer_signal &e) {
        PyErr_SetString(PyExc_ValueError, e.get_message().c_str());
        return NULL;
    }

    return Py_BuildValue("K", n_consumed);
}

static PyObject * hash_get_tags_and_positions(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * seq;

    if (!PyArg_ParseTuple(args, "s", &seq)) {
        return NULL;
    }

    // call the C++ function, and trap signals => Python

    std::vector<unsigned int> posns;
    std::vector<HashIntoType> tags;

    unsigned int pos = 1;
    KMerIterator kmers(seq, counting->ksize());

    while (!kmers.done()) {
        HashIntoType kmer = kmers.next();
        if (set_contains(counting->all_tags, kmer)) {
            posns.push_back(pos);
            tags.push_back(kmer);
        }
        pos++;
    }

    PyObject * posns_list = PyList_New(posns.size());
    for (size_t i = 0; i < posns.size(); i++) {
        PyObject * tup = Py_BuildValue("IK", posns[i], tags[i]);
        PyList_SET_ITEM(posns_list, i, tup);
    }

    return posns_list;
}

static PyObject * hash_find_all_tags_list(PyObject * self, PyObject *args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * kmer_s = NULL;

    if (!PyArg_ParseTuple(args, "s", &kmer_s)) {
        return NULL;
    }

    if (strlen(kmer_s) != counting->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "k-mer length must equal the counting table k-mer size");
        return NULL;
    }

    SeenSet tags;

    Py_BEGIN_ALLOW_THREADS

    HashIntoType kmer_f, kmer_r;
    _hash(kmer_s, counting->ksize(), kmer_f, kmer_r);

    counting->partition->find_all_tags(kmer_f, kmer_r, tags,
                                       counting->all_tags);

    Py_END_ALLOW_THREADS

    PyObject * x =  PyList_New(tags.size());
    if (x == NULL) {
        return NULL;
    }
    SeenSet::iterator si;
    unsigned long long i = 0;
    for (si = tags.begin(); si != tags.end(); ++si) {
        // type K for python unsigned long long
        PyList_SET_ITEM(x, i, Py_BuildValue("K", *si));
        i++;
    }

    return x;
}

static PyObject * hash_consume_fasta_and_tag(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * filename;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(args, "s|O", &filename, &callback_obj)) {
        return NULL;
    }

    // call the C++ function, and trap signals => Python

    unsigned long long n_consumed;
    unsigned int total_reads;

    try {
        counting->consume_fasta_and_tag(filename, total_reads, n_consumed,
                                        _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        PyErr_SetString(PyExc_IOError, e.get_message().c_str());
        return NULL;
    }

    return Py_BuildValue("IK", total_reads, n_consumed);
}

static PyObject * hash_find_all_tags_truncate_on_abundance(PyObject * self,
        PyObject *args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * kmer_s = NULL;
    BoundedCounterType min_count, max_count;

    if (!PyArg_ParseTuple(args, "sHH", &kmer_s, &min_count, &max_count)) {
        return NULL;
    }

    if (strlen(kmer_s) != counting->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "k-mer size must equal the k-mer size of the counting table");
        return NULL;
    }

    _pre_partition_info * ppi = NULL;

    Py_BEGIN_ALLOW_THREADS

    HashIntoType kmer, kmer_f, kmer_r;
    kmer = _hash(kmer_s, counting->ksize(), kmer_f, kmer_r);

    ppi = new _pre_partition_info(kmer);
    counting->partition->find_all_tags_truncate_on_abundance(kmer_f, kmer_r,
            ppi->tagged_kmers,
            counting->all_tags,
            min_count,
            max_count);
    counting->add_kmer_to_tags(kmer);

    Py_END_ALLOW_THREADS

    return PyCapsule_New((void *)ppi,
                         "khmer.pre_partition_info",
                         free_pre_partition_info);
}

static PyObject * hash_do_subset_partition_with_abundance(PyObject * self,
        PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    PyObject * callback_obj = NULL;
    HashIntoType start_kmer = 0, end_kmer = 0;
    PyObject * break_on_stop_tags_o = NULL;
    PyObject * stop_big_traversals_o = NULL;
    BoundedCounterType min_count, max_count;

    if (!PyArg_ParseTuple(args, "HH|KKOOO",
                          &min_count, &max_count,
                          &start_kmer, &end_kmer,
                          &break_on_stop_tags_o,
                          &stop_big_traversals_o,
                          &callback_obj)) {
        return NULL;
    }

    bool break_on_stop_tags = false;
    if (break_on_stop_tags_o && PyObject_IsTrue(break_on_stop_tags_o)) {
        break_on_stop_tags = true;
    }
    bool stop_big_traversals = false;
    if (stop_big_traversals_o && PyObject_IsTrue(stop_big_traversals_o)) {
        stop_big_traversals = true;
    }

    SubsetPartition * subset_p = NULL;
    try {
        Py_BEGIN_ALLOW_THREADS
        subset_p = new SubsetPartition(counting);
        subset_p->do_partition_with_abundance(start_kmer, end_kmer,
                                              min_count, max_count,
                                              break_on_stop_tags,
                                              stop_big_traversals,
                                              _report_fn, callback_obj);
        Py_END_ALLOW_THREADS
    } catch (_khmer_signal &e) {
        return NULL;
    }

    khmer_KSubsetPartitionObject * subset_obj = (khmer_KSubsetPartitionObject *)\
            PyObject_New(khmer_KSubsetPartitionObject, &khmer_KSubsetPartitionType);

    if (subset_obj == NULL) {
        delete subset_p;
        return NULL;
    }

    subset_obj->subset = subset_p;

    return (PyObject *) subset_obj;
}

static PyMethodDef khmer_counting_methods[] = {
    { "ksize", hash_get_ksize, METH_VARARGS, "" },
    { "hashsizes", hash_get_hashsizes, METH_VARARGS, "" },
    { "set_use_bigcount", hash_set_use_bigcount, METH_VARARGS, "" },
    { "get_use_bigcount", hash_get_use_bigcount, METH_VARARGS, "" },
    { "n_unique_kmers", hash_n_unique_kmers, METH_VARARGS, "Count the number of unique kmers" },
    { "n_occupied", hash_n_occupied, METH_VARARGS, "Count the number of occupied bins" },
    { "n_entries", hash_n_entries, METH_VARARGS, "" },
    { "count", hash_count, METH_VARARGS, "Count the given kmer" },
    { "consume", hash_consume, METH_VARARGS, "Count all k-mers in the given string" },
    { "consume_fasta", hash_consume_fasta, METH_VARARGS, "Count all k-mers in a given file" },
    {
        "consume_fasta_with_reads_parser", hash_consume_fasta_with_reads_parser,
        METH_VARARGS, "Count all k-mers using a given reads parser"
    },
    { "output_fasta_kmer_pos_freq", hash_output_fasta_kmer_pos_freq, METH_VARARGS, "" },
    { "get", hash_get, METH_VARARGS, "Get the count for the given k-mer" },
    { "get_min_count", hash_get_min_count, METH_VARARGS, "Get the smallest count of all the k-mers in the string" },
    { "get_max_count", hash_get_max_count, METH_VARARGS, "Get the largest count of all the k-mers in the string" },
    { "get_median_count", hash_get_median_count, METH_VARARGS, "Get the median, average, and stddev of the k-mer counts in the string" },
    { "get_kadian_count", hash_get_kadian_count, METH_VARARGS, "Get the kadian (abundance of k-th rank-ordered k-mer) of the k-mer counts in the string" },
    { "trim_on_abundance", count_trim_on_abundance, METH_VARARGS, "Trim on >= abundance" },
    { "trim_below_abundance", count_trim_below_abundance, METH_VARARGS, "Trim on >= abundance" },
    { "find_spectral_error_positions", count_find_spectral_error_positions, METH_VARARGS, "Identify positions of low-abundance k-mers" },
    { "abundance_distribution", hash_abundance_distribution, METH_VARARGS, "" },
    { "abundance_distribution_with_reads_parser", hash_abundance_distribution_with_reads_parser, METH_VARARGS, "" },
    { "fasta_count_kmers_by_position", hash_fasta_count_kmers_by_position, METH_VARARGS, "" },
    { "fasta_dump_kmers_by_abundance", hash_fasta_dump_kmers_by_abundance, METH_VARARGS, "" },
    { "load", hash_load, METH_VARARGS, "" },
    { "save", hash_save, METH_VARARGS, "" },
    {
        "collect_high_abundance_kmers", hash_collect_high_abundance_kmers,
        METH_VARARGS, ""
    },
    { "consume_and_tag", hash_consume_and_tag, METH_VARARGS, "Consume a sequence and tag it" },
    { "get_tags_and_positions", hash_get_tags_and_positions, METH_VARARGS, "Retrieve tags and their positions in a sequence." },
    { "find_all_tags_list", hash_find_all_tags_list, METH_VARARGS, "Find all tags within range of the given k-mer, return as list" },
    { "consume_fasta_and_tag", hash_consume_fasta_and_tag, METH_VARARGS, "Count all k-mers in a given file" },
    { "do_subset_partition_with_abundance", hash_do_subset_partition_with_abundance, METH_VARARGS, "" },
    { "find_all_tags_truncate_on_abundance", hash_find_all_tags_truncate_on_abundance, METH_VARARGS, "" },

    {NULL, NULL, 0, NULL}           /* sentinel */
};

#define is_counting_obj(v)  (Py_TYPE(v) == &khmer_KCountingHashType)

static PyTypeObject khmer_KCountingHashType
CPYCHECKER_TYPE_OBJECT_FOR_TYPEDEF("khmer_KCountingHashObject")
= {
    PyVarObject_HEAD_INIT(NULL, 0)
    "KCountingHash", sizeof(khmer_KCountingHashObject),
    0,
    khmer_counting_dealloc, /*tp_dealloc*/
    0,              /*tp_print*/
    0,              /*tp_getattr*/
    0,              /*tp_setattr*/
    0,              /*tp_compare*/
    0,              /*tp_repr*/
    0,              /*tp_as_number*/
    0,              /*tp_as_sequence*/
    0,              /*tp_as_mapping*/
    0,              /*tp_hash */
    0,              /*tp_call*/
    0,              /*tp_str*/
    0,              /*tp_getattro*/
    0,              /*tp_setattro*/
    0,              /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,     /*tp_flags*/
    "counting hash object",           /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    khmer_counting_methods,  /* tp_methods */
};

//
// new_hashtable
//

static PyObject* new_hashtable(PyObject * self, PyObject * args)
{
    unsigned int k = 0;
    unsigned long long size = 0;

    if (!PyArg_ParseTuple(args, "IK", &k, &size)) {
        return NULL;
    }

    khmer_KCountingHashObject * kcounting_obj = (khmer_KCountingHashObject *) \
            PyObject_New(khmer_KCountingHashObject, &khmer_KCountingHashType);

    if (kcounting_obj == NULL) {
        return NULL;
    }

    kcounting_obj->counting = new CountingHash(k, size);

    return (PyObject *) kcounting_obj;
}

//
// new_counting_hash
//

static PyObject* _new_counting_hash(PyObject * self, PyObject * args)
{
    WordLength k = 0;
    PyListObject * sizes_list_o = NULL;

    if (!PyArg_ParseTuple(args, "bO!", &k, &PyList_Type, &sizes_list_o)) {
        return NULL;
    }

    std::vector<HashIntoType> sizes;
    Py_ssize_t sizes_list_o_length = PyList_GET_SIZE(sizes_list_o);
    if (sizes_list_o_length == -1) {
        PyErr_SetString(PyExc_ValueError, "error with hashtable primes!");
        return NULL;
    }
    for (Py_ssize_t i = 0; i < sizes_list_o_length; i++) {
        PyObject * size_o = PyList_GET_ITEM(sizes_list_o, i);
        if (PyLong_Check(size_o)) {
            sizes.push_back((HashIntoType) PyLong_AsUnsignedLongLong(size_o));
        } else if (PyInt_Check(size_o)) {
            sizes.push_back((HashIntoType) PyInt_AsLong(size_o));
        } else if (PyFloat_Check(size_o)) {
            sizes.push_back((HashIntoType) PyFloat_AS_DOUBLE(size_o));
        } else {
            PyErr_SetString(PyExc_TypeError,
                            "2nd argument must be a list of ints, longs, or floats");
            return NULL;
        }
    }

    khmer_KCountingHashObject * kcounting_obj = (khmer_KCountingHashObject *) \
            PyObject_New(khmer_KCountingHashObject, &khmer_KCountingHashType);

    if (kcounting_obj == NULL) {
        return NULL;
    }

    kcounting_obj->counting = new CountingHash(k, sizes);

    return (PyObject *) kcounting_obj;
}

//
// hashbits stuff
//

static void khmer_hashbits_dealloc(PyObject * obj);
static PyObject* khmer_hashbits_new(PyTypeObject * type, PyObject * args,
                                    PyObject * kwds);
static int khmer_hashbits_init(khmer_KHashbitsObject * self, PyObject * args,
                               PyObject * kwds);

static PyTypeObject khmer_KHashbitsType
CPYCHECKER_TYPE_OBJECT_FOR_TYPEDEF("khmer_KHashbitsObject")
= {
    PyVarObject_HEAD_INIT(NULL, 0)
    "Hashbits", sizeof(khmer_KHashbitsObject),
    0,
    (destructor)khmer_hashbits_dealloc, /*tp_dealloc*/
    0,              /*tp_print*/
    0,              /*tp_getattr*/
    0,              /*tp_setattr*/
    0,              /*tp_compare*/
    0,              /*tp_repr*/
    0,              /*tp_as_number*/
    0,              /*tp_as_sequence*/
    0,              /*tp_as_mapping*/
    0,              /*tp_hash */
    0,              /*tp_call*/
    0,              /*tp_str*/
    0,              /*tp_getattro*/
    0,              /*tp_setattro*/
    0,              /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,       /*tp_flags*/
    "hashbits object",           /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    0,  /* tp_methods */
    0,                       /* tp_members */
    0,                       /* tp_getset */
    0,                       /* tp_base */
    0,                       /* tp_dict */
    0,                       /* tp_descr_get */
    0,                       /* tp_descr_set */
    0,                       /* tp_dictoffset */
    (initproc)khmer_hashbits_init,   /* tp_init */
    0,                       /* tp_alloc */
};

static PyObject * hash_abundance_distribution_with_reads_parser(
    PyObject * self,
    PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    khmer :: python :: khmer_ReadParser_Object * rparser_obj = NULL;
    khmer_KHashbitsObject *tracking_obj = NULL;

    if (!PyArg_ParseTuple(args, "O!O!", &python::khmer_ReadParser_Type,
                          &rparser_obj, &khmer_KHashbitsType, &tracking_obj)) {
        return NULL;
    }

    read_parsers:: IParser * rparser = rparser_obj->parser;
    Hashbits * hashbits = tracking_obj->hashbits;

    HashIntoType * dist = NULL;

    const char * exception = NULL;
    Py_BEGIN_ALLOW_THREADS
    try {
        dist = counting->abundance_distribution(rparser, hashbits);
    } catch (khmer::read_parsers::NoMoreReadsAvailable &exc ) {
        exception = exc.what();
    }
    Py_END_ALLOW_THREADS
    if (exception != NULL) {
        delete[] dist;
        PyErr_SetString(PyExc_IOError, exception);
        return NULL;
    }

    PyObject * x = PyList_New(MAX_BIGCOUNT + 1);
    if (x == NULL) {
        delete[] dist;
        return NULL;
    }
    for (int i = 0; i < MAX_BIGCOUNT + 1; i++) {
        PyList_SET_ITEM(x, i, PyLong_FromUnsignedLongLong(dist[i]));
    }

    delete[] dist;
    return x;
}

static PyObject * hash_abundance_distribution(PyObject * self, PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * filename = NULL;
    khmer_KHashbitsObject * tracking_obj = NULL;
    if (!PyArg_ParseTuple(args, "sO!", &filename, &khmer_KHashbitsType,
                          &tracking_obj)) {
        return NULL;
    }

    Hashbits * hashbits = tracking_obj->hashbits;
    HashIntoType * dist;

    char const * result = "";
    bool exception = false;
    Py_BEGIN_ALLOW_THREADS
    try {
        dist = counting->abundance_distribution(filename, hashbits);
    } catch (khmer_file_exception &e) {
        exception = true;
        result = e.what();
    }
    Py_END_ALLOW_THREADS

    if (exception) {
        PyErr_SetString(PyExc_IOError, result);
        return NULL;
    }

    PyObject * x = PyList_New(MAX_BIGCOUNT + 1);
    if (x == NULL) {
        delete[] dist;
        return NULL;
    }
    for (int i = 0; i < MAX_BIGCOUNT + 1; i++) {
        PyList_SET_ITEM(x, i, PyLong_FromUnsignedLongLong(dist[i]));
    }

    delete[] dist;

    return x;
}

static PyObject * hashbits_n_unique_kmers(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    HashIntoType n = hashbits->n_unique_kmers();

    return PyLong_FromUnsignedLongLong(n);
}


static PyObject * hashbits_count_overlap(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;
    khmer_KHashbitsObject * ht2_argu;
    const char * filename;
    PyObject * callback_obj = NULL;
    Hashbits * ht2;

    if (!PyArg_ParseTuple(args, "sO!|O", &filename, &khmer_KHashbitsType,
                          &ht2_argu,
                          &callback_obj)) {
        return NULL;
    }

    ht2 = ht2_argu->hashbits;

    // call the C++ function, and trap signals => Python

    unsigned long long n_consumed;
    unsigned int total_reads;
    HashIntoType curve[2][100];

    try {
        hashbits->consume_fasta_overlap(filename, curve, *ht2, total_reads, n_consumed,
                                        _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        PyErr_SetString(PyExc_IOError, e.get_message().c_str());
        return NULL;
    }

    HashIntoType n = hashbits->n_unique_kmers();
    HashIntoType n_overlap = hashbits->n_overlap_kmers();

    PyObject * x = PyList_New(200);

    for (unsigned int i = 0; i < 100; i++) {
        PyList_SetItem(x, i, Py_BuildValue("K", curve[0][i]));
    }
    for (unsigned int i = 0; i < 100; i++) {
        PyList_SetItem(x, i + 100, Py_BuildValue("K", curve[1][i]));
    }
    return Py_BuildValue("KKO", n, n_overlap, x);
}

static PyObject * hashbits_n_occupied(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    HashIntoType start = 0, stop = 0;

    if (!PyArg_ParseTuple(args, "|KK", &start, &stop)) {
        return NULL;
    }

    HashIntoType n = hashbits->n_occupied(start, stop);

    return PyLong_FromUnsignedLongLong(n);
}

static PyObject * hashbits_n_tags(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    return PyLong_FromSize_t(hashbits->n_tags());
}

static PyObject * hashbits_count(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * kmer;

    if (!PyArg_ParseTuple(args, "s", &kmer)) {
        return NULL;
    }

    if (strlen(kmer) != hashbits->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "k-mer length must equal the presence table k-mer size");
        return NULL;
    }

    hashbits->count(kmer);

    return PyLong_FromLong(1);
}

static PyObject * hashbits_consume(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * long_str;

    if (!PyArg_ParseTuple(args, "s", &long_str)) {
        return NULL;
    }

    if (strlen(long_str) < hashbits->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "string length must >= the hashbits k-mer size");
        return NULL;
    }

    unsigned int n_consumed;
    n_consumed = hashbits->consume_string(long_str);

    return PyLong_FromLong(n_consumed);
}

static PyObject * hashbits_print_stop_tags(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    hashbits->print_stop_tags(filename);

    Py_RETURN_NONE;
}

static PyObject * hashbits_print_tagset(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    hashbits->print_tagset(filename);

    Py_RETURN_NONE;
}

static PyObject * hashbits_load_stop_tags(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;
    PyObject * clear_tags_o = NULL;

    if (!PyArg_ParseTuple(args, "s|O", &filename, &clear_tags_o)) {
        return NULL;
    }

    bool clear_tags = true;
    if (clear_tags_o && !PyObject_IsTrue(clear_tags_o)) {
        clear_tags = false;
    }


    try {
        hashbits->load_stop_tags(filename, clear_tags);
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}


static PyObject * hashbits_save_stop_tags(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    hashbits->save_stop_tags(filename);

    Py_RETURN_NONE;
}

static PyObject * hashbits_traverse_from_tags(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    khmer_KCountingHashObject * counting_o = NULL;
    unsigned int distance, threshold, frequency;

    if (!PyArg_ParseTuple(args, "O!III", &khmer_KCountingHashType, &counting_o,
                          &distance, &threshold, &frequency)) {
        return NULL;
    }

    hashbits->traverse_from_tags(distance, threshold, frequency,
                                 * counting_o->counting);

    Py_RETURN_NONE;
}

static PyObject * hashbits_repartition_largest_partition(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    khmer_KCountingHashObject * counting_o = NULL;
    PyObject * subset_o = NULL;
    unsigned int distance, threshold, frequency;

    if (!PyArg_ParseTuple(args, "OO!III", &subset_o, &khmer_KCountingHashType,
                          &counting_o, &distance, &threshold, &frequency)) {
        return NULL;
    }

    SubsetPartition * subset_p;
    if (subset_o != Py_None) {
        subset_p = (SubsetPartition *) PyCapsule_GetPointer(subset_o, "khmer.SubsetPartition");
    } else {
        subset_p = hashbits->partition;
    }

    CountingHash * counting = counting_o->counting;

    unsigned long next_largest = subset_p->repartition_largest_partition(distance,
                                 threshold, frequency, *counting);

    return PyLong_FromLong(next_largest);
}

static PyObject * hashbits_get(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    PyObject * arg;

    if (!PyArg_ParseTuple(args, "O", &arg)) {
        return NULL;
    }

    unsigned long count = 0;

    if (PyInt_Check(arg)) {
        long pos = PyInt_AsLong(arg);
        count = hashbits->get_count((unsigned int) pos);
    } else if (PyBytes_Check(arg)) {
        std::string s = PyBytes_AsString(arg);

        if (strlen(s.c_str()) < hashbits->ksize()) {
            PyErr_SetString(PyExc_ValueError,
                            "string length must equal the presence table k-mer size");
            return NULL;
        }

        count = hashbits->get_count(s.c_str());
    } else {
        PyErr_SetString(PyExc_ValueError, "must pass in an int or string");
        return NULL;
    }

    return PyLong_FromLong(count);
}

static PyObject * hashbits_calc_connected_graph_size(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * _kmer;
    unsigned int max_size = 0;
    PyObject * break_on_circum_o = NULL;
    if (!PyArg_ParseTuple(args, "s|IO", &_kmer, &max_size, &break_on_circum_o)) {
        return NULL;
    }

    bool break_on_circum = false;
    if (break_on_circum_o && PyObject_IsTrue(break_on_circum_o)) {
        break_on_circum = true;
    }

    unsigned long long size = 0;

    Py_BEGIN_ALLOW_THREADS
    SeenSet keeper;
    hashbits->calc_connected_graph_size(_kmer, size, keeper, max_size,
                                        break_on_circum);
    Py_END_ALLOW_THREADS

    return PyLong_FromUnsignedLongLong(size);
}

static PyObject * hashbits_kmer_degree(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * kmer_s = NULL;

    if (!PyArg_ParseTuple(args, "s", &kmer_s)) {
        return NULL;
    }

    return PyLong_FromLong(hashbits->kmer_degree(kmer_s));
}

static PyObject * hashbits_trim_on_stoptags(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * seq = NULL;

    if (!PyArg_ParseTuple(args, "s", &seq)) {
        return NULL;
    }

    size_t trim_at;
    Py_BEGIN_ALLOW_THREADS

    trim_at = hashbits->trim_on_stoptags(seq);

    Py_END_ALLOW_THREADS;

    PyObject * trim_seq = PyBytes_FromStringAndSize(seq, trim_at);
    if (trim_seq == NULL) {
        return NULL;
    }
    PyObject * ret = Py_BuildValue("Ok", trim_seq, (unsigned long) trim_at);
    Py_DECREF(trim_seq);

    return ret;
}

static PyObject * hashbits_identify_stoptags_by_position(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * seq = NULL;

    if (!PyArg_ParseTuple(args, "s", &seq)) {
        return NULL;
    }

    std::vector<unsigned int> posns;
    Py_BEGIN_ALLOW_THREADS

    hashbits->identify_stop_tags_by_position(seq, posns);

    Py_END_ALLOW_THREADS;

    PyObject * x = PyList_New(posns.size());

    for (unsigned int i = 0; i < posns.size(); i++) {
        PyList_SET_ITEM(x, i, Py_BuildValue("I", posns[i]));
    }

    return x;
}

static PyObject * hashbits_do_subset_partition(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    PyObject * callback_obj = NULL;
    HashIntoType start_kmer = 0, end_kmer = 0;
    PyObject * break_on_stop_tags_o = NULL;
    PyObject * stop_big_traversals_o = NULL;

    if (!PyArg_ParseTuple(args, "|KKOOO", &start_kmer, &end_kmer,
                          &break_on_stop_tags_o,
                          &stop_big_traversals_o,
                          &callback_obj)) {
        return NULL;
    }

    bool break_on_stop_tags = false;
    if (break_on_stop_tags_o && PyObject_IsTrue(break_on_stop_tags_o)) {
        break_on_stop_tags = true;
    }
    bool stop_big_traversals = false;
    if (stop_big_traversals_o && PyObject_IsTrue(stop_big_traversals_o)) {
        stop_big_traversals = true;
    }

    SubsetPartition * subset_p = NULL;
    try {
        Py_BEGIN_ALLOW_THREADS
        subset_p = new SubsetPartition(hashbits);
        subset_p->do_partition(start_kmer, end_kmer, break_on_stop_tags,
                               stop_big_traversals,
                               _report_fn, callback_obj);
        Py_END_ALLOW_THREADS
    } catch (_khmer_signal &e) {
        return NULL;
    }

    return PyCapsule_New((void *)subset_p,
                         "khmer.SubsetPartition",
                         free_subset_partition_info);
}

static PyObject * hashbits_join_partitions_by_path(PyObject * self,
        PyObject *args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * sequence = NULL;
    if (!PyArg_ParseTuple(args, "s", &sequence)) {
        return NULL;
    }

    hashbits->partition->join_partitions_by_path(sequence);

    Py_RETURN_NONE;
}

static PyObject * hashbits_merge_subset(PyObject * self, PyObject *args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    PyObject * subset_obj;
    if (!PyArg_ParseTuple(args, "O", &subset_obj)) {
        return NULL;
    }

    if (!PyCapsule_IsValid(subset_obj, "khmer.SubsetPartition")) {
        PyErr_SetString( PyExc_ValueError, "invalid subset");
        return NULL;
    }

    SubsetPartition * subset_p;
    subset_p = (SubsetPartition *) PyCapsule_GetPointer(subset_obj, "khmer.SubsetPartition");

    hashbits->partition->merge(subset_p);

    Py_RETURN_NONE;
}

static PyObject * hashbits_merge_from_disk(PyObject * self, PyObject *args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;
    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    try {
        hashbits->partition->merge_from_disk(filename);
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject * hashbits_consume_fasta(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(args, "s|O", &filename, &callback_obj)) {
        return NULL;
    }

    // call the C++ function, and trap signals => Python

    unsigned long long n_consumed = 0;
    unsigned int total_reads = 0;

    try {
        hashbits->consume_fasta(filename, total_reads, n_consumed,
                                _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        PyErr_SetString(PyExc_IOError, e.get_message().c_str());
        return NULL;
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    return Py_BuildValue("IK", total_reads, n_consumed);
}

static PyObject * hashbits_consume_fasta_with_reads_parser(
    PyObject * self, PyObject * args
)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    PyObject * rparser_obj = NULL;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(
                args, "O|O", &rparser_obj, &callback_obj)) {
        return NULL;
    }

    read_parsers:: IParser * rparser =
        _PyObject_to_khmer_ReadParser( rparser_obj );

    // call the C++ function, and trap signals => Python
    unsigned long long  n_consumed  = 0;
    unsigned int          total_reads = 0;
    char const * exc = NULL;
    Py_BEGIN_ALLOW_THREADS
    try {
        hashbits->consume_fasta(rparser, total_reads, n_consumed,
                                _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        exc = e.get_message().c_str();
    }

    Py_END_ALLOW_THREADS
    if (exc != NULL) {
        PyErr_SetString(PyExc_IOError, exc);
        return NULL;
    }

    return Py_BuildValue("IK", total_reads, n_consumed);
}

static PyObject * hashbits_consume_fasta_and_traverse(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename;
    unsigned int radius, big_threshold, transfer_threshold;
    khmer_KCountingHashObject * counting_o = NULL;

    if (!PyArg_ParseTuple(args, "sIIIO!", &filename,
                          &radius, &big_threshold, &transfer_threshold,
                          &khmer_KCountingHashType, &counting_o)) {
        return NULL;
    }

    CountingHash * counting = counting_o->counting;

    hashbits->consume_fasta_and_traverse(filename, radius, big_threshold,
                                         transfer_threshold, *counting);


    Py_RETURN_NONE;
}

void sig(unsigned int total_reads, unsigned int n_consumed)
{
    std::cout << total_reads << " " << n_consumed << std::endl;
}

static PyObject * hashbits_consume_fasta_and_tag(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(args, "s|O", &filename, &callback_obj)) {
        return NULL;
    }

    // call the C++ function, and trap signals => Python

    unsigned long long n_consumed;
    unsigned int total_reads;

    try {
        hashbits->consume_fasta_and_tag(filename, total_reads, n_consumed,
                                        _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        PyErr_SetString(PyExc_IOError, e.get_message().c_str());
        return NULL;
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    return Py_BuildValue("IK", total_reads, n_consumed);
}

static PyObject * hashbits_consume_fasta_and_tag_with_reads_parser(
    PyObject * self, PyObject * args
)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    python::khmer_ReadParser_Object * rparser_obj = NULL;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple( args, "O!|O", &python::khmer_ReadParser_Type,
                           &rparser_obj, &callback_obj )) {
        return NULL;
    }

    read_parsers:: IParser * rparser = rparser_obj-> parser;

    // call the C++ function, and trap signals => Python
    unsigned long long  n_consumed  = 0;
    unsigned int          total_reads = 0;
    char const * exc = NULL;
    Py_BEGIN_ALLOW_THREADS
    try {
        hashbits->consume_fasta_and_tag(
            rparser, total_reads, n_consumed, _report_fn, callback_obj
        );
    } catch (_khmer_signal &e) {
        exc = e.get_message().c_str();
    } catch (khmer::read_parsers::NoMoreReadsAvailable &e) {
        exc = e.what();
    }
    Py_END_ALLOW_THREADS
    if (exc != NULL) {
        PyErr_SetString(PyExc_IOError, exc);
        return NULL;
    }

    return Py_BuildValue("IK", total_reads, n_consumed);
}

static PyObject * hashbits_consume_fasta_and_tag_with_stoptags(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(args, "s|O", &filename, &callback_obj)) {
        return NULL;
    }

    // call the C++ function, and trap signals => Python

    unsigned long long n_consumed;
    unsigned int total_reads;

    try {
        hashbits->consume_fasta_and_tag_with_stoptags(filename,
                total_reads, n_consumed,
                _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        PyErr_SetString(PyExc_IOError, e.get_message().c_str());
        return NULL;
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    return Py_BuildValue("IK", total_reads, n_consumed);
}

static PyObject * hashbits_consume_partitioned_fasta(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(args, "s|O", &filename, &callback_obj)) {
        return NULL;
    }

    // call the C++ function, and trap signals => Python

    unsigned long long n_consumed;
    unsigned int total_reads;

    try {
        hashbits->consume_partitioned_fasta(filename, total_reads, n_consumed,
                                            _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        PyErr_SetString(PyExc_IOError, e.get_message().c_str());
        return NULL;
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    return Py_BuildValue("IK", total_reads, n_consumed);
}

static PyObject * hashbits_find_all_tags(PyObject * self, PyObject *args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * kmer_s = NULL;

    if (!PyArg_ParseTuple(args, "s", &kmer_s)) {
        return NULL;
    }

    if (strlen(kmer_s) != hashbits->ksize()) {
        PyErr_SetString( PyExc_ValueError,
                         "k-mer size must equal the k-mer size of the presence table");
        return NULL;
    }

    _pre_partition_info * ppi = NULL;

    Py_BEGIN_ALLOW_THREADS

    HashIntoType kmer, kmer_f, kmer_r;
    kmer = _hash(kmer_s, hashbits->ksize(), kmer_f, kmer_r);

    ppi = new _pre_partition_info(kmer);
    hashbits->partition->find_all_tags(kmer_f, kmer_r, ppi->tagged_kmers,
                                       hashbits->all_tags);
    hashbits->add_kmer_to_tags(kmer);

    Py_END_ALLOW_THREADS

    return PyCapsule_New((void *)ppi,
                         "khmer.pre_partition_info",
                         free_pre_partition_info);
}

static PyObject * hashbits_assign_partition_id(PyObject * self, PyObject *args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    PyObject * ppi_obj;
    if (!PyArg_ParseTuple(args, "O", &ppi_obj)) {
        return NULL;
    }

    if (!PyCapsule_IsValid(ppi_obj, "khmer.pre_partition_info")) {
        PyErr_SetString( PyExc_ValueError, "invalid pre_partition_info");
        return NULL;
    }

    _pre_partition_info * ppi;
    ppi = (_pre_partition_info *) PyCapsule_GetPointer(ppi_obj, "khmer.pre_partition_info");

    PartitionID p;
    p = hashbits->partition->assign_partition_id(ppi->kmer,
            ppi->tagged_kmers);

    return PyLong_FromLong(p);
}

static PyObject * hashbits_add_tag(PyObject * self, PyObject *args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * kmer_s = NULL;
    if (!PyArg_ParseTuple(args, "s", &kmer_s)) {
        return NULL;
    }

    HashIntoType kmer = _hash(kmer_s, hashbits->ksize());
    hashbits->add_tag(kmer);

    Py_RETURN_NONE;
}

static PyObject * hashbits_add_stop_tag(PyObject * self, PyObject *args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * kmer_s = NULL;
    if (!PyArg_ParseTuple(args, "s", &kmer_s)) {
        return NULL;
    }

    HashIntoType kmer = _hash(kmer_s, hashbits->ksize());
    hashbits->add_stop_tag(kmer);

    Py_RETURN_NONE;
}

static PyObject * hashbits_get_stop_tags(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    WordLength k = hashbits->ksize();
    SeenSet::const_iterator si;

    PyObject * x = PyList_New(hashbits->stop_tags.size());
    unsigned long long i = 0;
    for (si = hashbits->stop_tags.begin(); si != hashbits->stop_tags.end(); si++) {
        std::string s = _revhash(*si, k);
        PyList_SET_ITEM(x, i, Py_BuildValue("s", s.c_str()));
        i++;
    }

    return x;
}

static PyObject * hashbits_get_tagset(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    WordLength k = hashbits->ksize();
    SeenSet::const_iterator si;

    PyObject * x = PyList_New(hashbits->all_tags.size());
    unsigned long long i = 0;
    for (si = hashbits->all_tags.begin(); si != hashbits->all_tags.end(); si++) {
        std::string s = _revhash(*si, k);
        PyList_SET_ITEM(x, i, Py_BuildValue("s", s.c_str()));
        i++;
    }

    return x;
}

static PyObject * hashbits_output_partitions(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;
    const char * output = NULL;
    PyObject * callback_obj = NULL;
    PyObject * output_unassigned_o = NULL;

    if (!PyArg_ParseTuple(args, "ss|OO", &filename, &output,
                          &output_unassigned_o,
                          &callback_obj)) {
        return NULL;
    }

    bool output_unassigned = false;
    if (output_unassigned_o != NULL && PyObject_IsTrue(output_unassigned_o)) {
        output_unassigned = true;
    }

    size_t n_partitions = 0;

    try {
        SubsetPartition * subset_p = hashbits->partition;
        n_partitions = subset_p->output_partitioned_file(filename,
                       output,
                       output_unassigned,
                       _report_fn,
                       callback_obj);
    } catch (_khmer_signal &e) {
        PyErr_SetString(PyExc_IOError, e.get_message().c_str());
        return NULL;
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    return PyLong_FromLong(n_partitions);
}

static PyObject * hashbits_find_unpart(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;
    PyObject * traverse_o = NULL;
    PyObject * stop_big_traversals_o = NULL;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(args, "sOO|O", &filename, &traverse_o,
                          &stop_big_traversals_o, &callback_obj)) {
        return NULL;
    }

    bool traverse = PyObject_IsTrue(traverse_o);
    bool stop_big_traversals = PyObject_IsTrue(stop_big_traversals_o);
    unsigned int n_singletons = 0;

    try {
        SubsetPartition * subset_p = hashbits->partition;
        n_singletons = subset_p->find_unpart(filename, traverse,
                                             stop_big_traversals,
                                             _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        return NULL;
    }

    return PyLong_FromLong(n_singletons);

    // Py_INCREF(Py_None);
    // return Py_None;
}

static PyObject * hashbits_filter_if_present(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;
    const char * output = NULL;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(args, "ss|O", &filename, &output, &callback_obj)) {
        return NULL;
    }

    try {
        hashbits->filter_if_present(filename, output, _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject * hashbits_save_partitionmap(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    hashbits->partition->save_partitionmap(filename);

    Py_RETURN_NONE;
}

static PyObject * hashbits_load_partitionmap(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    hashbits->partition->load_partitionmap(filename);

    Py_RETURN_NONE;
}

static PyObject * hashbits__validate_partitionmap(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    hashbits->partition->_validate_pmap();

    Py_RETURN_NONE;
}

static PyObject * hashbits_count_partitions(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    size_t n_partitions = 0, n_unassigned = 0;
    hashbits->partition->count_partitions(n_partitions, n_unassigned);

    return Py_BuildValue("nn", (Py_ssize_t) n_partitions,
                         (Py_ssize_t) n_unassigned);
}

static PyObject * hashbits_subset_count_partitions(PyObject * self,
        PyObject * args)
{
    PyObject * subset_obj = NULL;

    if (!PyArg_ParseTuple(args, "O", &subset_obj)) {
        return NULL;
    }

    SubsetPartition * subset_p;
    subset_p = (SubsetPartition *) PyCapsule_GetPointer(subset_obj, "khmer.SubsetPartition");

    size_t n_partitions = 0, n_unassigned = 0;
    subset_p->count_partitions(n_partitions, n_unassigned);

    return Py_BuildValue("nn", (Py_ssize_t) n_partitions,
                         (Py_ssize_t) n_unassigned);
}

static PyObject * hashbits_subset_partition_size_distribution(PyObject * self,
        PyObject * args)
{
    PyObject * subset_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &subset_obj)) {
        return NULL;
    }

    SubsetPartition * subset_p;
    subset_p = (SubsetPartition *) PyCapsule_GetPointer(subset_obj, "khmer.SubsetPartition");

    PartitionCountDistribution d;

    unsigned int n_unassigned = 0;
    subset_p->partition_size_distribution(d, n_unassigned);

    PyObject * x = PyList_New(d.size());
    if (x == NULL) {
        return NULL;
    }
    PartitionCountDistribution::iterator di;

    unsigned int i;
    for (i = 0, di = d.begin(); di != d.end(); di++, i++) {
        PyObject * value =  Py_BuildValue("KK", di->first, di->second);
        if (value == NULL) {
            Py_DECREF(x);
            return NULL;
        }
        PyList_SET_ITEM(x, i, value);
    }
    if (!(i == d.size())) {
        throw khmer_exception();
    }

    PyObject * returnValue = Py_BuildValue("NI", x, n_unassigned);
    if (returnValue == NULL) {
        Py_DECREF(x);
        return NULL;
    }
    return returnValue;
}

static PyObject * hashbits_load(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    try {
        hashbits->load(filename);
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject * hashbits_save(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    hashbits->save(filename);

    Py_RETURN_NONE;
}

static PyObject * hashbits_load_tagset(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;
    PyObject * clear_tags_o = NULL;

    if (!PyArg_ParseTuple(args, "s|O", &filename, &clear_tags_o)) {
        return NULL;
    }

    bool clear_tags = true;
    if (clear_tags_o && !PyObject_IsTrue(clear_tags_o)) {
        clear_tags = false;
    }

    try {
        hashbits->load_tagset(filename, clear_tags);
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject * hashbits_save_tagset(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    hashbits->save_tagset(filename);

    Py_RETURN_NONE;
}

static PyObject * hashbits_save_subset_partitionmap(PyObject * self,
        PyObject * args)
{
    const char * filename = NULL;
    PyObject * subset_obj = NULL;

    if (!PyArg_ParseTuple(args, "Os", &subset_obj, &filename)) {
        return NULL;
    }

    SubsetPartition * subset_p;
    subset_p = (SubsetPartition *) PyCapsule_GetPointer(subset_obj, "khmer.SubsetPartition");

    Py_BEGIN_ALLOW_THREADS

    subset_p->save_partitionmap(filename);

    Py_END_ALLOW_THREADS

    Py_RETURN_NONE;
}

static PyObject * hashbits_load_subset_partitionmap(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * filename = NULL;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    SubsetPartition * subset_p;
    subset_p = new SubsetPartition(hashbits);

    bool fail = false;
    std::string err;

    Py_BEGIN_ALLOW_THREADS

    try {
        subset_p->load_partitionmap(filename);
    } catch (khmer_file_exception &e) {
        fail = true;
        err = e.what();
    }

    Py_END_ALLOW_THREADS

    if (fail) {
        PyErr_SetString(PyExc_IOError, err.c_str());
        delete subset_p;
        return NULL;
    } else {
        return PyCapsule_New((void *)subset_p,
                             "khmer.SubsetPartition",
                             free_subset_partition_info);
    }
}

static PyObject * hashbits__set_tag_density(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    unsigned int d;
    if (!PyArg_ParseTuple(args, "I", &d)) {
        return NULL;
    }

    hashbits->_set_tag_density(d);

    Py_RETURN_NONE;
}

static PyObject * hashbits__get_tag_density(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    unsigned int d = hashbits->_get_tag_density();

    return PyLong_FromLong(d);
}

static PyObject * hashbits__validate_subset_partitionmap(PyObject * self,
        PyObject * args)
{
    PyObject * subset_obj = NULL;

    if (!PyArg_ParseTuple(args, "O", &subset_obj)) {
        return NULL;
    }

    SubsetPartition * subset_p;
    subset_p = (SubsetPartition *) PyCapsule_GetPointer(subset_obj, "khmer.SubsetPartition");
    subset_p->_validate_pmap();

    Py_RETURN_NONE;
}

static PyObject * hashbits_set_partition_id(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * kmer = NULL;
    PartitionID p = 0;

    if (!PyArg_ParseTuple(args, "sI", &kmer, &p)) {
        return NULL;
    }

    hashbits->partition->set_partition_id(kmer, p);

    Py_RETURN_NONE;
}

static PyObject * hashbits_join_partitions(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    PartitionID p1 = 0, p2 = 0;

    if (!PyArg_ParseTuple(args, "II", &p1, &p2)) {
        return NULL;
    }

    p1 = hashbits->partition->join_partitions(p1, p2);

    return PyLong_FromLong(p1);
}

static PyObject * hashbits_get_partition_id(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * kmer = NULL;

    if (!PyArg_ParseTuple(args, "s", &kmer)) {
        return NULL;
    }

    PartitionID partition_id;
    partition_id = hashbits->partition->get_partition_id(kmer);

    return PyLong_FromLong(partition_id);
}

static PyObject * hashbits_is_single_partition(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * seq = NULL;

    if (!PyArg_ParseTuple(args, "s", &seq)) {
        return NULL;
    }

    bool v = hashbits->partition->is_single_partition(seq);

    PyObject * val;
    if (v) {
        val = Py_True;
    } else {
        val = Py_False;
    }
    Py_INCREF(val);

    return val;
}

static PyObject * hashbits_divide_tags_into_subsets(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    unsigned int subset_size = 0;

    if (!PyArg_ParseTuple(args, "I", &subset_size)) {
        return NULL;
    }

    SeenSet divvy;
    hashbits->divide_tags_into_subsets(subset_size, divvy);

    PyObject * x = PyList_New(divvy.size());
    unsigned int i = 0;
    for (SeenSet::const_iterator si = divvy.begin(); si != divvy.end();
            si++, i++) {
        PyList_SET_ITEM(x, i, PyLong_FromUnsignedLongLong(*si));
    }

    return x;
}

static PyObject * hashbits_count_kmers_within_radius(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * kmer = NULL;
    unsigned int radius = 0;
    unsigned int max_count = 0;

    if (!PyArg_ParseTuple(args, "sI|I", &kmer, &radius, &max_count)) {
        return NULL;
    }

    unsigned int n;

    Py_BEGIN_ALLOW_THREADS

    HashIntoType kmer_f, kmer_r;
    _hash(kmer, hashbits->ksize(), kmer_f, kmer_r);
    n = hashbits->count_kmers_within_radius(kmer_f, kmer_r, radius,
                                            max_count);

    Py_END_ALLOW_THREADS

    return PyLong_FromUnsignedLong(n);
}

static PyObject * hashbits_get_ksize(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    unsigned int k = hashbits->ksize();

    return PyLong_FromLong(k);
}


static PyObject * hashbits_get_hashsizes(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    std::vector<HashIntoType> ts = hashbits->get_tablesizes();

    PyObject * x = PyList_New(ts.size());
    for (size_t i = 0; i < ts.size(); i++) {
        PyList_SET_ITEM(x, i, PyLong_FromUnsignedLongLong(ts[i]));
    }

    return x;
}

static PyObject * hashbits_extract_unique_paths(PyObject * self,
        PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * sequence = NULL;
    unsigned int min_length = 0;
    float min_unique_f = 0;
    if (!PyArg_ParseTuple(args, "sIf", &sequence, &min_length, &min_unique_f)) {
        return NULL;
    }

    std::vector<std::string> results;
    hashbits->extract_unique_paths(sequence, min_length, min_unique_f, results);

    PyObject * x = PyList_New(results.size());
    if (x == NULL) {
        return NULL;
    }

    for (unsigned int i = 0; i < results.size(); i++) {
        PyList_SET_ITEM(x, i, PyBytes_FromString(results[i].c_str()));
    }

    return x;
}

static PyObject * hashbits_get_median_count(PyObject * self, PyObject * args)
{
    khmer_KHashbitsObject * me = (khmer_KHashbitsObject *) self;
    Hashbits * hashbits = me->hashbits;

    const char * long_str;

    if (!PyArg_ParseTuple(args, "s", &long_str)) {
        return NULL;
    }

    if (strlen(long_str) < hashbits->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "string length must >= the hashtable k-mer size");
        return NULL;
    }

    BoundedCounterType med = 0;
    float average = 0, stddev = 0;

    hashbits->get_median_count(long_str, med, average, stddev);

    return Py_BuildValue("iff", med, average, stddev);
}

static PyMethodDef khmer_hashbits_methods[] = {
    { "extract_unique_paths", hashbits_extract_unique_paths, METH_VARARGS, "" },
    { "ksize", hashbits_get_ksize, METH_VARARGS, "" },
    { "hashsizes", hashbits_get_hashsizes, METH_VARARGS, "" },
    { "n_occupied", hashbits_n_occupied, METH_VARARGS, "Count the number of occupied bins" },
    { "n_unique_kmers", hashbits_n_unique_kmers,  METH_VARARGS, "Count the number of unique kmers" },
    { "count", hashbits_count, METH_VARARGS, "Count the given kmer" },
    { "count_overlap", hashbits_count_overlap, METH_VARARGS, "Count overlap kmers in two datasets" },
    { "consume", hashbits_consume, METH_VARARGS, "Count all k-mers in the given string" },
    { "load_stop_tags", hashbits_load_stop_tags, METH_VARARGS, "" },
    { "save_stop_tags", hashbits_save_stop_tags, METH_VARARGS, "" },
    { "print_stop_tags", hashbits_print_stop_tags, METH_VARARGS, "" },
    { "print_tagset", hashbits_print_tagset, METH_VARARGS, "" },
    { "get", hashbits_get, METH_VARARGS, "Get the count for the given k-mer" },
    { "calc_connected_graph_size", hashbits_calc_connected_graph_size, METH_VARARGS, "" },
    { "kmer_degree", hashbits_kmer_degree, METH_VARARGS, "" },
    { "trim_on_stoptags", hashbits_trim_on_stoptags, METH_VARARGS, "" },
    { "identify_stoptags_by_position", hashbits_identify_stoptags_by_position, METH_VARARGS, "" },
    { "do_subset_partition", hashbits_do_subset_partition, METH_VARARGS, "" },
    { "find_all_tags", hashbits_find_all_tags, METH_VARARGS, "" },
    { "assign_partition_id", hashbits_assign_partition_id, METH_VARARGS, "" },
    { "output_partitions", hashbits_output_partitions, METH_VARARGS, "" },
    { "find_unpart", hashbits_find_unpart, METH_VARARGS, "" },
    { "filter_if_present", hashbits_filter_if_present, METH_VARARGS, "" },
    { "add_tag", hashbits_add_tag, METH_VARARGS, "" },
    { "add_stop_tag", hashbits_add_stop_tag, METH_VARARGS, "" },
    { "get_stop_tags", hashbits_get_stop_tags, METH_VARARGS, "" },
    { "get_tagset", hashbits_get_tagset, METH_VARARGS, "" },
    { "load", hashbits_load, METH_VARARGS, "" },
    { "save", hashbits_save, METH_VARARGS, "" },
    { "load_tagset", hashbits_load_tagset, METH_VARARGS, "" },
    { "save_tagset", hashbits_save_tagset, METH_VARARGS, "" },
    { "n_tags", hashbits_n_tags, METH_VARARGS, "" },
    { "divide_tags_into_subsets", hashbits_divide_tags_into_subsets, METH_VARARGS, "" },
    { "load_partitionmap", hashbits_load_partitionmap, METH_VARARGS, "" },
    { "save_partitionmap", hashbits_save_partitionmap, METH_VARARGS, "" },
    { "_validate_partitionmap", hashbits__validate_partitionmap, METH_VARARGS, "" },
    { "_get_tag_density", hashbits__get_tag_density, METH_VARARGS, "" },
    { "_set_tag_density", hashbits__set_tag_density, METH_VARARGS, "" },
    { "consume_fasta", hashbits_consume_fasta, METH_VARARGS, "Count all k-mers in a given file" },
    { "consume_fasta_with_reads_parser", hashbits_consume_fasta_with_reads_parser, METH_VARARGS, "Count all k-mers in a given file" },
    { "consume_fasta_and_tag", hashbits_consume_fasta_and_tag, METH_VARARGS, "Count all k-mers in a given file" },
    {
        "consume_fasta_and_tag_with_reads_parser", hashbits_consume_fasta_and_tag_with_reads_parser,
        METH_VARARGS, "Count all k-mers using a given reads parser"
    },
    { "consume_fasta_and_traverse", hashbits_consume_fasta_and_traverse, METH_VARARGS, "" },
    { "consume_fasta_and_tag_with_stoptags", hashbits_consume_fasta_and_tag_with_stoptags, METH_VARARGS, "Count all k-mers in a given file" },
    { "consume_partitioned_fasta", hashbits_consume_partitioned_fasta, METH_VARARGS, "Count all k-mers in a given file" },
    { "join_partitions_by_path", hashbits_join_partitions_by_path, METH_VARARGS, "" },
    { "merge_subset", hashbits_merge_subset, METH_VARARGS, "" },
    { "merge_subset_from_disk", hashbits_merge_from_disk, METH_VARARGS, "" },
    { "count_partitions", hashbits_count_partitions, METH_VARARGS, "" },
    { "subset_count_partitions", hashbits_subset_count_partitions, METH_VARARGS, "" },
    { "subset_partition_size_distribution", hashbits_subset_partition_size_distribution, METH_VARARGS, "" },
    { "save_subset_partitionmap", hashbits_save_subset_partitionmap, METH_VARARGS },
    { "load_subset_partitionmap", hashbits_load_subset_partitionmap, METH_VARARGS },
    { "_validate_subset_partitionmap", hashbits__validate_subset_partitionmap, METH_VARARGS, "" },
    { "set_partition_id", hashbits_set_partition_id, METH_VARARGS, "" },
    { "join_partitions", hashbits_join_partitions, METH_VARARGS, "" },
    { "get_partition_id", hashbits_get_partition_id, METH_VARARGS, "" },
    { "is_single_partition", hashbits_is_single_partition, METH_VARARGS, "" },
    { "count_kmers_within_radius", hashbits_count_kmers_within_radius, METH_VARARGS, "" },
    { "traverse_from_tags", hashbits_traverse_from_tags, METH_VARARGS, "" },
    { "repartition_largest_partition", hashbits_repartition_largest_partition, METH_VARARGS, "" },
    { "get_median_count", hashbits_get_median_count, METH_VARARGS, "Get the median, average, and stddev of the k-mer counts in the string" },
    {NULL, NULL, 0, NULL}           /* sentinel */
};

// __new__ for hashbits; necessary for proper subclassing
// This will essentially do what the old factory function did. Unlike many __new__
// methods, we take our arguments here, because there's no "uninitialized" hashbits
// object; we have to have k and the table sizes before creating the new objects
static PyObject* khmer_hashbits_new(PyTypeObject * type, PyObject * args,
                                    PyObject * kwds)
{
    khmer_KHashbitsObject * self;
    self = (khmer_KHashbitsObject *)type->tp_alloc(type, 0);

    if (self != NULL) {
        WordLength k = 0;
        PyListObject* sizes_list_o = NULL;

        if (!PyArg_ParseTuple(args, "bO!", &k, &PyList_Type, &sizes_list_o)) {
            Py_DECREF(self);
            return NULL;
        }

        std::vector<HashIntoType> sizes;
        Py_ssize_t sizes_list_o_length = PyList_GET_SIZE(sizes_list_o);
        for (Py_ssize_t i = 0; i < sizes_list_o_length; i++) {
            PyObject * size_o = PyList_GET_ITEM(sizes_list_o, i);
            if (PyLong_Check(size_o)) {
                sizes.push_back((HashIntoType) PyLong_AsUnsignedLongLong(size_o));
            } else if (PyInt_Check(size_o)) {
                sizes.push_back((HashIntoType) PyInt_AsLong(size_o));
            } else if (PyFloat_Check(size_o)) {
                sizes.push_back((HashIntoType) PyFloat_AS_DOUBLE(size_o));
            } else {
                Py_DECREF(self);
                PyErr_SetString(PyExc_TypeError,
                                "2nd argument must be a list of ints, longs, or floats");
                return NULL;
            }
        }

        self->hashbits = new Hashbits(k, sizes);
    }
    return (PyObject *) self;
}

// there are no attributes that we need at this time, so we'll just return 0
static int khmer_hashbits_init(khmer_KHashbitsObject * self, PyObject * args,
                               PyObject * kwds)
{
    return 0;
}

#define is_hashbits_obj(v)  (Py_TYPE(v) == &khmer_KHashbitsType)

////////////////////////////////////////////////////////////////////////////

static PyObject * subset_count_partitions(PyObject * self,
        PyObject * args)
{
    khmer_KSubsetPartitionObject * me = (khmer_KSubsetPartitionObject *) self;
    SubsetPartition * subset_p = me->subset;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    size_t n_partitions = 0, n_unassigned = 0;
    subset_p->count_partitions(n_partitions, n_unassigned);

    return Py_BuildValue("nn", (Py_ssize_t) n_partitions,
                         (Py_ssize_t) n_unassigned);
}

static PyObject * subset_report_on_partitions(PyObject * self,
        PyObject * args)
{
    khmer_KSubsetPartitionObject * me = (khmer_KSubsetPartitionObject *) self;
    SubsetPartition * subset_p = me->subset;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    subset_p->report_on_partitions();

    Py_RETURN_NONE;
}

static PyObject * subset_compare_partitions(PyObject * self,
        PyObject * args)
{
    khmer_KSubsetPartitionObject * me = (khmer_KSubsetPartitionObject *) self;
    SubsetPartition * subset1_p = me->subset;

    PyObject * subset2_obj = NULL;
    PartitionID pid1, pid2; // @CTB ensure that these are unsigned?

    if (!PyArg_ParseTuple(args, "IOI",
                          &pid1, &subset2_obj, &pid2)) {
        return NULL;
    }

    khmer_KSubsetPartitionObject *other = (khmer_KSubsetPartitionObject *)
                                          subset2_obj;
    SubsetPartition * subset2_p = other->subset;

    unsigned int n_only1 = 0, n_only2 = 0, n_shared = 0;
    subset1_p->compare_to_partition(pid1, subset2_p, pid2,
                                    n_only1, n_only2, n_shared);

    return Py_BuildValue("III", n_only1, n_only2, n_shared);
}

static PyObject * subset_partition_size_distribution(PyObject * self,
        PyObject * args)
{
    khmer_KSubsetPartitionObject * me = (khmer_KSubsetPartitionObject *) self;
    SubsetPartition * subset_p = me->subset;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    PartitionCountDistribution d;

    unsigned int n_unassigned = 0;
    subset_p->partition_size_distribution(d, n_unassigned);

    PyObject * x = PyList_New(d.size());
    if (x == NULL) {
        return NULL;
    }
    PartitionCountDistribution::iterator di;

    unsigned int i;
    for (i = 0, di = d.begin(); di != d.end(); di++, i++) {
        PyObject * tup = Py_BuildValue("KK", di->first, di->second);
        if (tup != NULL) {
            PyList_SET_ITEM(x, i, tup);
        }
        Py_XDECREF(tup);
    }
    if (!(i == d.size())) {
        throw khmer_exception();
    }

    PyObject * ret = Py_BuildValue("OI", x, n_unassigned);
    Py_DECREF(x);
    return ret;
}

static PyObject * subset_partition_sizes(PyObject * self,
        PyObject * args)
{
    khmer_KSubsetPartitionObject * me = (khmer_KSubsetPartitionObject *) self;
    SubsetPartition * subset_p = me->subset;

    unsigned int min_size = 0;

    if (!PyArg_ParseTuple(args, "|I", &min_size)) {
        return NULL;
    }

    PartitionCountMap cm;
    unsigned int n_unassigned = 0;
    subset_p->partition_sizes(cm, n_unassigned);

    unsigned int i = 0;
    PartitionCountMap::const_iterator mi;
    for (mi = cm.begin(); mi != cm.end(); mi++) {
        if (mi->second >= min_size) {
            i++;
        }
    }

    PyObject * x = PyList_New(i);
    if (x == NULL) {
        return NULL;
    }

    // this should probably be a dict. @CTB
    for (i = 0, mi = cm.begin(); mi != cm.end(); mi++) {
        if (mi->second >= min_size) {
            PyObject * tup = Py_BuildValue("II", mi->first, mi->second);
            if (tup != NULL) {
                PyList_SET_ITEM(x, i, tup);
            }
            i++;
        }
    }

    PyObject * ret = Py_BuildValue("OI", x, n_unassigned);
    Py_DECREF(x);

    return ret;
}

static PyObject * subset_partition_average_coverages(PyObject * self,
        PyObject * args)
{
    khmer_KSubsetPartitionObject * me = (khmer_KSubsetPartitionObject *) self;
    SubsetPartition * subset_p = me->subset;

    khmer_KCountingHashObject * counting_o;

    if (!PyArg_ParseTuple(args, "O!", &khmer_KCountingHashType, &counting_o)) {
        return NULL;
    }

    PartitionCountMap cm;
    subset_p->partition_average_coverages(cm, counting_o -> counting);

    unsigned int i;
    PartitionCountMap::iterator mi;

    PyObject * x = PyList_New(cm.size());
    if (x == NULL) {
        return NULL;
    }

    // this should probably be a dict. @CTB
    for (i = 0, mi = cm.begin(); mi != cm.end(); mi++, i++) {
        PyObject * tup = Py_BuildValue("II", mi->first, mi->second);
        if (tup != NULL) {
            PyList_SET_ITEM(x, i, tup);
        }
    }

    return x;
}

static PyMethodDef khmer_subset_methods[] = {
    { "count_partitions", subset_count_partitions, METH_VARARGS, "" },
    { "report_on_partitions", subset_report_on_partitions, METH_VARARGS, "" },
    { "compare_partitions", subset_compare_partitions, METH_VARARGS, "" },
    { "partition_size_distribution", subset_partition_size_distribution, METH_VARARGS, "" },
    { "partition_sizes", subset_partition_sizes, METH_VARARGS, "" },
    { "partition_average_coverages", subset_partition_average_coverages, METH_VARARGS, "" },
    {NULL, NULL, 0, NULL}           /* sentinel */
};

/////////////////
// LabelHash
/////////////////

// LabelHash addition
typedef struct {
    //PyObject_HEAD
    khmer_KHashbitsObject khashbits;
    LabelHash * labelhash;
} khmer_KLabelHashObject;

static void khmer_labelhash_dealloc(PyObject *);
static int khmer_labelhash_init(khmer_KLabelHashObject * self, PyObject *args,
                                PyObject *kwds);
static PyObject * khmer_labelhash_new(PyTypeObject * type, PyObject *args,
                                      PyObject *kwds);

#define is_labelhash_obj(v)  (Py_TYPE(v) == &khmer_KLabelHashType)

//
// khmer_labelhash_dealloc -- clean up a labelhash object.
//

static void khmer_labelhash_dealloc(PyObject* obj)
{
    khmer_KLabelHashObject * self = (khmer_KLabelHashObject *) obj;

    delete self->labelhash;
    self->labelhash = NULL;

    Py_TYPE(obj)->tp_free((PyObject*)obj);
    //PyObject_Del((PyObject *) obj);
}

// a little weird; we don't actually want to call Hashbits' new method. Rather, we
// define our own new method, and redirect the base's hashbits object to point to our
// labelhash object
static PyObject * khmer_labelhash_new(PyTypeObject *type, PyObject *args,
                                      PyObject *kwds)
{
    khmer_KLabelHashObject *self;
    self = (khmer_KLabelHashObject*)type->tp_alloc(type, 0);

    if (self != NULL) {
        WordLength k = 0;
        PyListObject * sizes_list_o = NULL;

        if (!PyArg_ParseTuple(args, "bO!", &k, &PyList_Type, &sizes_list_o)) {
            Py_DECREF(self);
            return NULL;
        }

        std::vector<HashIntoType> sizes;
        Py_ssize_t sizes_list_o_length = PyList_GET_SIZE(sizes_list_o);
        for (Py_ssize_t i = 0; i < sizes_list_o_length; i++) {
            PyObject * size_o = PyList_GET_ITEM(sizes_list_o, i);
            if (PyLong_Check(size_o)) {
                sizes.push_back((HashIntoType) PyLong_AsUnsignedLongLong(size_o));
            } else if (PyInt_Check(size_o)) {
                sizes.push_back((HashIntoType) PyInt_AsLong(size_o));
            } else if (PyFloat_Check(size_o)) {
                sizes.push_back((HashIntoType) PyFloat_AS_DOUBLE(size_o));
            } else {
                Py_DECREF(self);
                PyErr_SetString(PyExc_TypeError,
                                "2nd argument must be a list of ints, longs, or floats");
                return NULL;
            }
        }


        // We want the hashbits pointer in the base class to point to our labelhash,
        // so that the KHashbits methods are called on the correct object (a LabelHash)
        self->labelhash = new LabelHash(k, sizes);
        self->khashbits.hashbits = (Hashbits *)self->labelhash;
    }

    return (PyObject *) self;
}

static int khmer_labelhash_init(khmer_KLabelHashObject * self, PyObject *args,
                                PyObject *kwds)
{
    if (khmer_KHashbitsType.tp_init((PyObject *)self, args, kwds) < 0) {
        return -1;
    }
    //std::cout << "testing my pointer ref to hashbits: " << self->khashbits.hashbits->n_tags() << std::endl;
    //std::cout << "hashbits: " << self->khashbits.hashbits << std::endl;
    //std::cout << "labelhash: " << self->labelhash << std::endl;
    return 0;
}

static PyObject * labelhash_get_label_dict(PyObject * self, PyObject * args)
{
    khmer_KLabelHashObject * me = (khmer_KLabelHashObject *) self;
    LabelHash * hb = me->labelhash;

    PyObject * d = PyDict_New();
    if (d == NULL) {
        return NULL;
    }
    LabelPtrMap::iterator it;

    for (it = hb->label_ptrs.begin(); it != hb->label_ptrs.end(); ++it) {
        PyObject * key = Py_BuildValue("K", it->first);
        PyObject * val = Py_BuildValue("K", it->second);
        if (key != NULL && val != NULL) {
            PyDict_SetItem(d, key, val);
        }
        Py_XDECREF(key);
        Py_XDECREF(val);
    }

    return d;
}

static PyObject * labelhash_consume_fasta_and_tag_with_labels(
    PyObject * self, PyObject * args)
{
    khmer_KLabelHashObject * me = (khmer_KLabelHashObject *) self;
    LabelHash * hb = me->labelhash;

    std::ofstream outfile;

    const char * filename;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(args, "s|O", &filename, &callback_obj)) {
        return NULL;
    }

    unsigned long long n_consumed;
    unsigned int total_reads;
    char const * exc = NULL;
    //Py_BEGIN_ALLOW_THREADS
    try {
        hb->consume_fasta_and_tag_with_labels(filename, total_reads,
                                              n_consumed, _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        exc = e.get_message().c_str();
    } catch (khmer_file_exception &e) {
        exc = e.what();
    }
    //Py_END_ALLOW_THREADS
    if (exc != NULL) {
        PyErr_SetString(PyExc_IOError, exc);
        return NULL;
    }

    return Py_BuildValue("IK", total_reads, n_consumed);

}

static PyObject * labelhash_consume_partitioned_fasta_and_tag_with_labels(
    PyObject * self, PyObject * args)
{
    khmer_KLabelHashObject * me = (khmer_KLabelHashObject *) self;
    LabelHash * labelhash = me->labelhash;

    const char * filename;
    PyObject * callback_obj = NULL;

    if (!PyArg_ParseTuple(args, "s|O", &filename, &callback_obj)) {
        return NULL;
    }

    // call the C++ function, and trap signals => Python

    unsigned long long n_consumed;
    unsigned int total_reads;

    try {
        labelhash->consume_partitioned_fasta_and_tag_with_labels(filename,
                total_reads, n_consumed, _report_fn, callback_obj);
    } catch (_khmer_signal &e) {
        PyErr_SetString(PyExc_IOError,
                        "error parsing in consume_partitioned_fasta_and_tag_with_labels");
        return NULL;
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }
    return Py_BuildValue("IK", total_reads, n_consumed);
}

static PyObject * labelhash_consume_sequence_and_tag_with_labels(
    PyObject * self, PyObject * args)
{
    khmer_KLabelHashObject * me = (khmer_KLabelHashObject *) self;
    LabelHash * hb = me->labelhash;
    const char * seq = NULL;
    unsigned long long c = 0;
    if (!PyArg_ParseTuple(args, "sK", &seq, &c)) {
        return NULL;
    }
    unsigned long long n_consumed = 0;
    Label * the_label = hb->check_and_allocate_label(c);

    try {
        hb->consume_sequence_and_tag_with_labels(seq, n_consumed, *the_label);
    } catch (_khmer_signal &e) {
        return NULL;
    }
    return Py_BuildValue("K", n_consumed);
}

static PyObject * labelhash_sweep_label_neighborhood(PyObject * self,
        PyObject * args)
{
    khmer_KLabelHashObject * me = (khmer_KLabelHashObject *) self;
    LabelHash * hb = me->labelhash;

    const char * seq = NULL;
    int r = 0;
    PyObject * break_on_stop_tags_o = NULL;
    PyObject * stop_big_traversals_o = NULL;

    if (!PyArg_ParseTuple(args, "s|iOO", &seq, &r,
                          &break_on_stop_tags_o,
                          &stop_big_traversals_o)) {
        return NULL;
    }

    unsigned int range = (2 * hb->_get_tag_density()) + 1;
    if (r >= 0) {
        range = r;
    }

    bool break_on_stop_tags = false;
    if (break_on_stop_tags_o && PyObject_IsTrue(break_on_stop_tags_o)) {
        break_on_stop_tags = true;
    }
    bool stop_big_traversals = false;
    if (stop_big_traversals_o && PyObject_IsTrue(stop_big_traversals_o)) {
        stop_big_traversals = true;
    }

    if (strlen(seq) < hb->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "string length must >= the hashtable k-mer size");
        return NULL;
    }

    //std::pair<TagLabelPtrPair::iterator, TagLabelPtrPair::iterator> ret;
    LabelPtrSet found_labels;

    bool exc_raised = false;
    //unsigned int num_traversed = 0;
    //Py_BEGIN_ALLOW_THREADS
    try {
        hb->sweep_label_neighborhood(seq, found_labels, range, break_on_stop_tags,
                                     stop_big_traversals);
    } catch (_khmer_signal &e) {
        exc_raised = true;
    }
    //Py_END_ALLOW_THREADS

    //printf("...%u kmers traversed\n", num_traversed);

    if (exc_raised) {
        return NULL;
    }

    PyObject * x =  PyList_New(found_labels.size());
    LabelPtrSet::const_iterator si;
    unsigned long long i = 0;
    for (si = found_labels.begin(); si != found_labels.end(); ++si) {
        PyList_SET_ITEM(x, i, Py_BuildValue("K", *(*si)));
        i++;
    }

    return x;
}

// Similar to find_all_tags, but returns tags in a way actually usable by python
// need a tags_in_sequence iterator or function in c++ land for reuse in all
// these functions
static PyObject * labelhash_sweep_tag_neighborhood(PyObject * self,
        PyObject *args)
{
    khmer_KLabelHashObject * me = (khmer_KLabelHashObject *) self;
    LabelHash * labelhash = me->labelhash;

    const char * seq = NULL;
    int r = 0;
    PyObject * break_on_stop_tags_o = NULL;
    PyObject * stop_big_traversals_o = NULL;

    if (!PyArg_ParseTuple(args, "s|iOO", &seq, &r,
                          &break_on_stop_tags_o,
                          &stop_big_traversals_o)) {
        return NULL;
    }

    unsigned int range = (2 * labelhash->_get_tag_density()) + 1;
    if (r >= 0) {
        range = r;
    }

    bool break_on_stop_tags = false;
    if (break_on_stop_tags_o && PyObject_IsTrue(break_on_stop_tags_o)) {
        break_on_stop_tags = true;
    }
    bool stop_big_traversals = false;
    if (stop_big_traversals_o && PyObject_IsTrue(stop_big_traversals_o)) {
        stop_big_traversals = true;
    }

    if (strlen(seq) < labelhash->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "string length must >= the hashtable k-mer size");
        return NULL;
    }

    SeenSet tagged_kmers;

    //Py_BEGIN_ALLOW_THREADS

    labelhash->partition->sweep_for_tags(seq, tagged_kmers,
                                         labelhash->all_tags, range, break_on_stop_tags, stop_big_traversals);

    //Py_END_ALLOW_THREADS

    PyObject * x =  PyList_New(tagged_kmers.size());
    if (x == NULL) {
        return NULL;
    }
    SeenSet::iterator si;
    unsigned long long i = 0;
    for (si = tagged_kmers.begin(); si != tagged_kmers.end(); ++si) {
        //std::string kmer_s = _revhash(*si, labelhash->ksize());
        // type K for python unsigned long long
        PyList_SET_ITEM(x, i, Py_BuildValue("K", *si));
        i++;
    }

    return x;
}


static PyObject * labelhash_get_tag_labels(PyObject * self, PyObject * args)
{

    khmer_KLabelHashObject * me = (khmer_KLabelHashObject *) self;
    LabelHash * labelhash = me->labelhash;

    HashIntoType tag;

    if (!PyArg_ParseTuple(args, "K", &tag)) {
        return NULL;
    }

    LabelPtrSet labels;

    labels = labelhash->get_tag_labels(tag);

    PyObject * x =  PyList_New(labels.size());
    LabelPtrSet::const_iterator si;
    unsigned long long i = 0;
    for (si = labels.begin(); si != labels.end(); ++si) {
        //std::string kmer_s = _revhash(*si, labelhash->ksize());
        PyList_SET_ITEM(x, i, Py_BuildValue("K", *(*si)));
        i++;
    }

    return x;
}

static PyObject * labelhash_n_labels(PyObject * self, PyObject * args)
{
    khmer_KLabelHashObject * me = (khmer_KLabelHashObject *) self;
    LabelHash * labelhash = me->labelhash;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    return PyLong_FromSize_t(labelhash->n_labels());
}

static PyMethodDef khmer_labelhash_methods[] = {
    { "consume_fasta_and_tag_with_labels", labelhash_consume_fasta_and_tag_with_labels, METH_VARARGS, "" },
    { "sweep_label_neighborhood", labelhash_sweep_label_neighborhood, METH_VARARGS, "" },
    {"consume_partitioned_fasta_and_tag_with_labels", labelhash_consume_partitioned_fasta_and_tag_with_labels, METH_VARARGS, "" },
    {"sweep_tag_neighborhood", labelhash_sweep_tag_neighborhood, METH_VARARGS, "" },
    {"get_tag_labels", labelhash_get_tag_labels, METH_VARARGS, ""},
    {"consume_sequence_and_tag_with_labels", labelhash_consume_sequence_and_tag_with_labels, METH_VARARGS, "" },
    {"n_labels", labelhash_n_labels, METH_VARARGS, ""},
    {"get_label_dict", labelhash_get_label_dict, METH_VARARGS, "" },

    {NULL, NULL, 0, NULL}           /* sentinel */
};

static PyTypeObject khmer_KLabelHashType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_LabelHash",            /* tp_name */
    sizeof(khmer_KLabelHashObject), /* tp_basicsize */
    0,                       /* tp_itemsize */
    (destructor)khmer_labelhash_dealloc, /* tp_dealloc */
    0,                       /* tp_print */
    0,  /* khmer_labelhash_getattr, tp_getattr */
    0,                       /* tp_setattr */
    0,                       /* tp_compare */
    0,                       /* tp_repr */
    0,                       /* tp_as_number */
    0,                       /* tp_as_sequence */
    0,                       /* tp_as_mapping */
    0,                       /* tp_hash */
    0,                       /* tp_call */
    0,                       /* tp_str */
    0,                       /* tp_getattro */
    0,                       /* tp_setattro */
    0,                       /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /* tp_flags */
    0,                       /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    khmer_labelhash_methods, /* tp_methods */
    0,                       /* tp_members */
    0,                       /* tp_getset */
    0,                       /* tp_base */
    0,                       /* tp_dict */
    0,                       /* tp_descr_get */
    0,                       /* tp_descr_set */
    0,                       /* tp_dictoffset */
    (initproc)khmer_labelhash_init,   /* tp_init */
    0,                       /* tp_alloc */
};

static PyObject * readaligner_align(khmer_ReadAligner_Object * me,
                                    PyObject * args)
{
    const char * read;

    if (!PyArg_ParseTuple(args, "s", &read)) {
        return NULL;
    }

    /*if (strlen(read) < (unsigned int)aligner->ksize()) {
        PyErr_SetString(PyExc_ValueError,
                        "string length must >= the hashtable k-mer size");
        return NULL;
    }*/

    Alignment * aln = me->aligner->Align(read);

    const char* alignment = aln->graph_alignment.c_str();
    const char* readAlignment = aln->read_alignment.c_str();
    PyObject * ret = Py_BuildValue("dssO", aln->score, alignment,
                                   readAlignment, (aln->truncated)? Py_True : Py_False);
    delete aln;

    return ret;
}

static PyMethodDef khmer_ReadAligner_methods[] = {
    {"align", (PyCFunction)readaligner_align, METH_VARARGS, ""},
    {NULL} /* Sentinel */
};

//
// khmer_readaligner_dealloc -- clean up readaligner object
// GRAPHALIGN addition
//
static void khmer_readaligner_dealloc(khmer_ReadAligner_Object* obj)
{
    delete obj->aligner;
    obj->aligner = NULL;
    Py_TYPE(obj)->tp_free((PyObject*)obj);
}

//
// new_readaligner
//
static PyObject* khmer_ReadAligner_new(PyTypeObject *type, PyObject * args,
                                       PyObject *kwds)
{
    khmer_ReadAligner_Object * self;

    self = (khmer_ReadAligner_Object *)type->tp_alloc(type, 0);

    if (self != NULL) {
        khmer_KCountingHashObject * ch = NULL;
        unsigned short int trusted_cov_cutoff = 2;
        double bits_theta = 1;

        if(!PyArg_ParseTuple(args, "O!Hd", &khmer_KCountingHashType, &ch,
                             &trusted_cov_cutoff, &bits_theta)) {
            Py_DECREF(self);
            return NULL;
        }

        self->aligner = new ReadAligner(ch->counting, trusted_cov_cutoff,
                                        bits_theta);
    }

    return (PyObject *) self;
}

static PyTypeObject khmer_ReadAlignerType = {
    PyVarObject_HEAD_INIT(NULL, 0) /* init & ob_size */
    "khmer.ReadAligner",		    /*tp_name*/
    sizeof(khmer_ReadAligner_Object),	    /*tp_basicsize*/
    0,					    /*tp_itemsize*/
    (destructor)khmer_readaligner_dealloc,  /*tp_dealloc*/
    0,                          /*tp_print*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_compare*/
    0,                          /*tp_repr*/
    0,                          /*tp_as_number*/
    0,                          /*tp_as_sequence*/
    0,                          /*tp_as_mapping*/
    0,                          /*tp_hash */
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    0,                          /*tp_getattro*/
    0,                          /*tp_setattro*/
    0,                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,         /*tp_flags*/
    "ReadAligner object",           /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    khmer_ReadAligner_methods, /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,			               /* tp_init */
    0,                         /* tp_alloc */
    khmer_ReadAligner_new,     /* tp_new */
};


//
// new_hashbits
//

static PyObject* _new_hashbits(PyObject * self, PyObject * args)
{
    WordLength k = 0;
    PyListObject * sizes_list_o = NULL;

    if (!PyArg_ParseTuple(args, "bO!", &k, &PyList_Type, &sizes_list_o)) {
        return NULL;
    }

    std::vector<HashIntoType> sizes;
    Py_ssize_t sizes_list_o_length = PyList_GET_SIZE(sizes_list_o);
    for (Py_ssize_t i = 0; i < sizes_list_o_length; i++) {
        PyObject * size_o = PyList_GET_ITEM(sizes_list_o, i);
        if (PyLong_Check(size_o)) {
            sizes.push_back((HashIntoType) PyLong_AsUnsignedLongLong(size_o));
        } else if (PyInt_Check(size_o)) {
            sizes.push_back((HashIntoType) PyInt_AsLong(size_o));
        } else if (PyFloat_Check(size_o)) {
            sizes.push_back((HashIntoType) PyFloat_AS_DOUBLE(size_o));
        } else {
            PyErr_SetString(PyExc_TypeError,
                            "2nd argument must be a list of ints, longs, or floats");
            return NULL;
        }
    }


    khmer_KHashbitsObject * khashbits_obj = (khmer_KHashbitsObject *) \
                                            PyObject_New(khmer_KHashbitsObject, &khmer_KHashbitsType);

    if (khashbits_obj == NULL) {
        return NULL;
    }

    khashbits_obj->hashbits = new Hashbits(k, sizes);

    return (PyObject *) khashbits_obj;
}

static PyObject * hash_collect_high_abundance_kmers(PyObject * self,
        PyObject * args)
{
    khmer_KCountingHashObject * me = (khmer_KCountingHashObject *) self;
    CountingHash * counting = me->counting;

    const char * filename = NULL;
    unsigned int lower_count, upper_count;

    if (!PyArg_ParseTuple(args, "sII", &filename, &lower_count, &upper_count)) {
        return NULL;
    }

    SeenSet found_kmers;
    counting->collect_high_abundance_kmers(filename, lower_count, upper_count,
                                           found_kmers);

    // create a new hashbits object...
    std::vector<HashIntoType> sizes;
    sizes.push_back(1);

    khmer_KHashbitsObject * khashbits_obj = (khmer_KHashbitsObject *) \
                                            PyObject_New(khmer_KHashbitsObject, &khmer_KHashbitsType);
    if (khashbits_obj == NULL) {
        return NULL;
    }

    // ...and set the collected kmers as the stoptags.
    khashbits_obj->hashbits = new Hashbits(counting->ksize(), sizes);
    khashbits_obj->hashbits->stop_tags.swap(found_kmers);

    return (PyObject *) khashbits_obj;
}

//
// khmer_counting_dealloc -- clean up a counting hash object.
//

static void khmer_counting_dealloc(PyObject* self)
{
    khmer_KCountingHashObject * obj = (khmer_KCountingHashObject *) self;
    delete obj->counting;
    obj->counting = NULL;

    PyObject_Del((PyObject *) obj);
}

//
// khmer_hashbits_dealloc -- clean up a hashbits object.
//
static void khmer_hashbits_dealloc(PyObject* obj)
{
    khmer_KHashbitsObject * self = (khmer_KHashbitsObject *) obj;

    delete self->hashbits;
    self->hashbits = NULL;

    Py_TYPE(obj)->tp_free((PyObject*)obj);
    //PyObject_Del((PyObject *) obj);
}


//
// khmer_subset_dealloc -- clean up a hashbits object.
//

static void khmer_subset_dealloc(PyObject* self)
{
    khmer_KSubsetPartitionObject * obj = (khmer_KSubsetPartitionObject *) self;
    delete obj->subset;
    obj->subset = NULL;

    PyObject_Del((PyObject *) obj);
}


/***********************************************************************/

//
// KHLLCounter object
//

typedef struct {
    PyObject_HEAD
    khmer::HLLCounter * hllcounter;
} khmer_KHLLCounter_Object;

static PyObject* khmer_hllcounter_new(PyTypeObject * type, PyObject * args,
                                      PyObject * kwds)
{
    khmer_KHLLCounter_Object * self;
    self = (khmer_KHLLCounter_Object *)type->tp_alloc(type, 0);

    if (self != NULL) {
        double error_rate = 0;
        WordLength ksize = 0;

        if (!PyArg_ParseTuple(args, "db", &error_rate, &ksize)) {
            Py_DECREF(self);
            return NULL;
        }

        if ((error_rate < 0) || (error_rate > 1.0)) {
            Py_DECREF(self);
            PyErr_SetString(PyExc_ValueError,
                            "Error rate should be between 0.0 and 1.0");
            return NULL;
        }

        try {
            self->hllcounter = new HLLCounter(error_rate, ksize);
        } catch (khmer_exception &e) {
            Py_DECREF(self);
            PyErr_SetString(PyExc_ValueError, e.what());
            return NULL;
        }
    }

    return (PyObject *) self;
}

//
// khmer_hllcounter_dealloc -- clean up a hllcounter object.
//

static void khmer_hllcounter_dealloc(khmer_KHLLCounter_Object * obj)
{
    delete obj->hllcounter;
    obj->hllcounter = NULL;

    Py_TYPE(obj)->tp_free((PyObject*)obj);
}

static
PyObject *
hllcounter_add(khmer_KHLLCounter_Object * me, PyObject * args)
{
    const char * kmer_str;

    if (!PyArg_ParseTuple(args, "s", &kmer_str)) {
        return NULL;
    }

    try {
        me->hllcounter->add(kmer_str);
    } catch (khmer_exception &e) {
        PyErr_SetString(PyExc_ValueError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static
PyObject *
hllcounter_estimate_cardinality(khmer_KHLLCounter_Object * me, PyObject * args)
{
    if (!PyArg_ParseTuple( args, "" )) {
        return NULL;
    }

    return PyLong_FromLong(me->hllcounter->estimate_cardinality());
}

static
PyObject *
hllcounter_consume_string(khmer_KHLLCounter_Object * me, PyObject * args)
{
    const char * kmer_str;
    unsigned long long n_consumed;

    if (!PyArg_ParseTuple(args, "s", &kmer_str)) {
        return NULL;
    }

    try {
        n_consumed = me->hllcounter->consume_string(kmer_str);
    } catch (khmer_exception &e) {
        PyErr_SetString(PyExc_ValueError, e.what());
        return NULL;
    }

    return PyLong_FromLong(n_consumed);
}

static PyObject * hllcounter_consume_fasta(khmer_KHLLCounter_Object * me,
        PyObject * args)
{
    const char * filename;

    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    // call the C++ function, and trap signals => Python
    unsigned long long  n_consumed    = 0;
    unsigned int        total_reads   = 0;
    try {
        me->hllcounter->consume_fasta(filename, total_reads, n_consumed);
    } catch (_khmer_signal &e) {
        PyErr_SetString(PyExc_IOError, e.get_message().c_str());
        return NULL;
    } catch (khmer_file_exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }

    return Py_BuildValue("IK", total_reads, n_consumed);
}

static PyMethodDef khmer_hllcounter_methods[] = {
    {
        "add", (PyCFunction)hllcounter_add,
        METH_VARARGS,
        "Add a k-mer to the counter."
    },
    {
        "estimate_cardinality", (PyCFunction)hllcounter_estimate_cardinality,
        METH_VARARGS,
        "Return the current estimation."
    },
    {
        "consume_string", (PyCFunction)hllcounter_consume_string,
        METH_VARARGS,
        "Break a sequence into k-mers and add each k-mer to the counter."
    },
    {
        "consume_fasta", (PyCFunction)hllcounter_consume_fasta,
        METH_VARARGS,
        "Read sequences from file, break into k-mers, "
        "and add each k-mer to the counter."
    },
    {NULL} /* Sentinel */
};

static PyTypeObject khmer_KHLLCounter_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "khmer.KHLLCounter",                       /* tp_name */
    sizeof(khmer_KHLLCounter_Object),          /* tp_basicsize */
    0,                                         /* tp_itemsize */
    (destructor)khmer_hllcounter_dealloc,      /* tp_dealloc */
    0,                                         /* tp_print */
    0,                                         /* tp_getattr */
    0,                                         /* tp_setattr */
    0,                                         /* tp_compare */
    0,                                         /* tp_repr */
    0,                                         /* tp_as_number */
    0,                                         /* tp_as_sequence */
    0,                                         /* tp_as_mapping */
    0,                                         /* tp_hash */
    0,                                         /* tp_call */
    0,                                         /* tp_str */
    0,                                         /* tp_getattro */
    0,                                         /* tp_setattro */
    0,                                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,  /* tp_flags */
    "HyperLogLog counter",                     /* tp_doc */
    0,                                         /* tp_traverse */
    0,                                         /* tp_clear */
    0,                                         /* tp_richcompare */
    0,                                         /* tp_weaklistoffset */
    0,                                         /* tp_iter */
    0,                                         /* tp_iternext */
    khmer_hllcounter_methods,                  /* tp_methods */
    0,                                         /* tp_members */
    0,                                         /* tp_getset */
    0,                                         /* tp_base */
    0,                                         /* tp_dict */
    0,                                         /* tp_descr_get */
    0,                                         /* tp_descr_set */
    0,                                         /* tp_dictoffset */
    0,                                         /* tp_init */
    0,                                         /* tp_alloc */
    khmer_hllcounter_new,                      /* tp_new */
};

#define is_hllcounter_obj(v)  (Py_TYPE(v) == &khmer_KHLLCounter_Type)


//////////////////////////////
// standalone functions

static PyObject * forward_hash(PyObject * self, PyObject * args)
{
    const char * kmer;
    WordLength ksize;

    if (!PyArg_ParseTuple(args, "sb", &kmer, &ksize)) {
        return NULL;
    }

    if (ksize > KSIZE_MAX) {
        PyErr_Format(PyExc_ValueError, "k-mer size must be <= %u", KSIZE_MAX);
        return NULL;
    }

    return PyLong_FromUnsignedLongLong(_hash(kmer, ksize));
}

static PyObject * forward_hash_no_rc(PyObject * self, PyObject * args)
{
    const char * kmer;
    WordLength ksize;

    if (!PyArg_ParseTuple(args, "sb", &kmer, &ksize)) {
        return NULL;
    }

    if (ksize > KSIZE_MAX) {
        PyErr_Format(PyExc_ValueError, "k-mer size must be <= %u", KSIZE_MAX);
        return NULL;
    }

    if (strlen(kmer) != ksize) {
        PyErr_SetString(PyExc_ValueError,
                        "k-mer length must equal the k-size");
        return NULL;
    }

    return PyLong_FromUnsignedLongLong(_hash_forward(kmer, ksize));
}

static PyObject * reverse_hash(PyObject * self, PyObject * args)
{
    HashIntoType val;
    WordLength ksize;

    if (!PyArg_ParseTuple(args, "Kb", &val, &ksize)) {
        return NULL;
    }

    if (ksize > KSIZE_MAX) {
        PyErr_Format(PyExc_ValueError, "k-mer size must be <= %u", KSIZE_MAX);
        return NULL;
    }

    return PyBytes_FromString(_revhash(val, ksize).c_str());
}

static PyObject * murmur3_forward_hash(PyObject * self, PyObject * args)
{
    const char * kmer;

    if (!PyArg_ParseTuple(args, "s", &kmer)) {
        return NULL;
    }

    return PyLong_FromUnsignedLongLong(_hash_murmur(kmer));
}

static PyObject * murmur3_forward_hash_no_rc(PyObject * self, PyObject * args)
{
    const char * kmer;

    if (!PyArg_ParseTuple(args, "s", &kmer)) {
        return NULL;
    }

    return PyLong_FromUnsignedLongLong(_hash_murmur_forward(kmer));
}

static PyObject * set_reporting_callback(PyObject * self, PyObject * args)
{
    PyObject * o;

    if (!PyArg_ParseTuple(args, "O", &o)) {
        return NULL;
    }

    Py_XDECREF(_callback_obj);
    Py_INCREF(o);
    _callback_obj = o;

    Py_RETURN_NONE;
}

//
// technique for resolving literal below found here:
// https://gcc.gnu.org/onlinedocs/gcc-4.9.1/cpp/Stringification.html
//

static
PyObject *
get_version_cpp( PyObject * self, PyObject * args )
{
#define xstr(s) str(s)
#define str(s) #s
    std::string dVersion = xstr(VERSION);
    return PyBytes_FromString(dVersion.c_str());
}


//
// Module machinery.
//

static PyMethodDef KhmerMethods[] = {
#if (0)
    {
        "new_config",       new_config,
        METH_VARARGS,       "Create a default internals config"
    },
#endif
#if (0)
    {
        "set_config",       set_active_config,
        METH_VARARGS,       "Set active khmer configuration object"
    },
#endif
    {
        "new_hashtable",        new_hashtable,
        METH_VARARGS,       "Create an empty single-table counting hash"
    },
    {
        "_new_counting_hash",   _new_counting_hash,
        METH_VARARGS,       "Create an empty counting hash"
    },
    {
        "_new_hashbits",        _new_hashbits,
        METH_VARARGS,       "Create an empty hashbits table"
    },
    {
        "forward_hash",     forward_hash,
        METH_VARARGS,       "",
    },
    {
        "forward_hash_no_rc",   forward_hash_no_rc,
        METH_VARARGS,       "",
    },
    {
        "reverse_hash",     reverse_hash,
        METH_VARARGS,       "",
    },
    {
        "hash_murmur3",
        murmur3_forward_hash,
        METH_VARARGS,
        "Calculate the hash value of a k-mer using MurmurHash3 "
        "(with reverse complement)",
    },
    {
        "hash_no_rc_murmur3",
        murmur3_forward_hash_no_rc,
        METH_VARARGS,
        "Calculate the hash value of a k-mer using MurmurHash3 "
        "(no reverse complement)",
    },
    {
        "set_reporting_callback",   set_reporting_callback,
        METH_VARARGS,       "",
    },
    {
        "get_version_cpp", get_version_cpp,
        METH_VARARGS, "return the VERSION c++ compiler option"
    },
    { NULL, NULL, 0, NULL } // sentinel
};

PyMODINIT_FUNC
init_khmer(void)
{
    using namespace python;

    if (PyType_Ready(&khmer_KCountingHashType) < 0) {
        return;
    }

    khmer_KSubsetPartitionType.tp_methods = khmer_subset_methods;
    if (PyType_Ready(&khmer_KSubsetPartitionType) < 0) {
        return;
    }

    // implemented __new__ for Hashbits; keeping factory func around as well
    // for backwards compat with old scripts
    khmer_KHashbitsType.tp_new = khmer_hashbits_new;
    khmer_KHashbitsType.tp_methods = khmer_hashbits_methods;
    if (PyType_Ready(&khmer_KHashbitsType) < 0) {
        return;
    }
    // add LabelHash

    khmer_KLabelHashType.tp_base = &khmer_KHashbitsType;
    khmer_KLabelHashType.tp_new = khmer_labelhash_new;
    if (PyType_Ready(&khmer_KLabelHashType) < 0) {
        return;
    }

    if (PyType_Ready(&khmer_ReadAlignerType) < 0) {
        return;
    }

    if (PyType_Ready(&khmer_KHLLCounter_Type) < 0) {
        return;
    }
    if (PyType_Ready(&khmer_ReadAlignerType) < 0) {
        return;
    }

    if (PyType_Ready( &khmer_ReadParser_Type ) < 0) {
        return;
    }

    if (PyType_Ready(&khmer_Read_Type ) < 0) {
        return;
    }

    if (PyType_Ready(&khmer_ReadPairIterator_Type ) < 0) {
        return;
    }

    PyObject * m;
    m = Py_InitModule3( "_khmer", KhmerMethods,
                        "interface for the khmer module low-level extensions" );
    if (m == NULL) {
        return;
    }

    Py_INCREF(&khmer_ReadParser_Type);
    if (PyModule_AddObject( m, "ReadParser", (PyObject *)&khmer_ReadParser_Type ) < 0) {
        return;
    }

    Py_INCREF(&khmer_KHashbitsType);
    PyModule_AddObject(m, "_Hashbits", (PyObject *)&khmer_KHashbitsType);

    Py_INCREF(&khmer_KLabelHashType);
    PyModule_AddObject(m, "_LabelHash", (PyObject *)&khmer_KLabelHashType);

    Py_INCREF(&khmer_KHLLCounter_Type);
    PyModule_AddObject(m, "_HLLCounter", (PyObject *)&khmer_KHLLCounter_Type);
    Py_INCREF(&khmer_ReadAlignerType);
    PyModule_AddObject(m, "ReadAligner", (PyObject *)&khmer_ReadAlignerType);
}

// vim: set ft=cpp sts=4 sw=4 tw=79:
