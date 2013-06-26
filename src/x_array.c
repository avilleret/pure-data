/* Copyright (c) 1997-2013 Miller Puckette and others.
* For information on usage and redistribution, and for a DISCLAIMER OF ALL
* WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* The "array" object. */

#include "m_pd.h"
#include "g_canvas.h"
#include <string.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef _WIN32
#include <io.h>
#endif
extern t_pd *newest;    /* OK - this should go into a .h file now :) */

#ifdef HAVE_ALLOCA_H        /* ifdef nonsense to find include for alloca() */
# include <alloca.h>        /* linux, mac, mingw, cygwin */
#elif defined _MSC_VER
# include <malloc.h>        /* MSVC */
#else
# include <stddef.h>        /* BSDs for example */
#endif                      /* end alloca() ifdef nonsense */

#ifndef HAVE_ALLOCA     /* can work without alloca() but we never need it */
#define HAVE_ALLOCA 1
#endif
#define TEXT_NGETBYTE 100 /* bigger that this we use alloc, not alloca */
#if HAVE_ALLOCA
#define ATOMS_ALLOCA(x, n) ((x) = (t_atom *)((n) < TEXT_NGETBYTE ?  \
        alloca((n) * sizeof(t_atom)) : getbytes((n) * sizeof(t_atom))))
#define ATOMS_FREEA(x, n) ( \
    ((n) < TEXT_NGETBYTE || (freebytes((x), (n) * sizeof(t_atom)), 0)))
#else
#define ATOMS_ALLOCA(x, n) ((x) = (t_atom *)getbytes((n) * sizeof(t_atom)))
#define ATOMS_FREEA(x, n) (freebytes((x), (n) * sizeof(t_atom)))
#endif

/* -- "table" - classic "array define" object by Guenter Geiger --*/

static int tabcount = 0;

static void *table_donew(t_symbol *s, t_floatarg f, t_floatarg flags)
{
    t_atom a[9];
    t_glist *gl;
    t_canvas *x, *z = canvas_getcurrent();
    if (s == &s_)
    {
         char  tabname[255];
         t_symbol *t = gensym("table"); 
         sprintf(tabname, "%s%d", t->s_name, tabcount++);
         s = gensym(tabname); 
    }
    if (f <= 1)
        f = 100;
    SETFLOAT(a, 0);
    SETFLOAT(a+1, 50);
    SETFLOAT(a+2, 600);
    SETFLOAT(a+3, 400);
    SETSYMBOL(a+4, s);
    SETFLOAT(a+5, 0);
    x = canvas_new(0, 0, 6, a);

    x->gl_owner = z;

        /* create a graph for the table */
    gl = glist_addglist((t_glist*)x, &s_, 0, -1, (f > 1 ? f-1 : 1), 1,
        50, 350, 550, 50);

    graph_array(gl, s, &s_float, f, flags);

    newest = &x->gl_pd;     /* mimic action of canvas_pop() */
    pd_popsym(&x->gl_pd);
    x->gl_loading = 0;

    return (x);
}

static void *table_new(t_symbol *s, t_floatarg f)
{
    return (table_donew(s, f, 0));
}

    /* return true if the "canvas" object is a "table". */
int canvas_istable(t_canvas *x)
{
    t_atom *argv = (x->gl_obj.te_binbuf? binbuf_getvec(x->gl_obj.te_binbuf):0);
    int argc = (x->gl_obj.te_binbuf? binbuf_getnatom(x->gl_obj.te_binbuf) : 0);
    int istable = (argc && argv[0].a_type == A_SYMBOL &&
        argv[0].a_w.w_symbol == gensym("table"));
    return (istable);
}

t_class *array_define_class;

static void *array_define_new(t_symbol *s, int argc, t_atom *argv)
{
    t_symbol *arrayname = &s_;
    float arraysize = 100;
    t_glist *x;
    int keep = 0;
    while (argc && argv->a_type == A_SYMBOL &&
        *argv->a_w.w_symbol->s_name == '-')
    {
        if (!strcmp(argv->a_w.w_symbol->s_name, "-k"))
            keep = 1;
        else
        {
            error("array define: unknown flag ...");
            postatom(argc, argv);
        }
        argc--; argv++;
    }
    if (argc && argv->a_type == A_SYMBOL)
    {
        arrayname = argv->a_w.w_symbol;
        argc--; argv++;
    }
    if (argc && argv->a_type == A_FLOAT)
    {
        arraysize = argv->a_w.w_float;
        argc--; argv++;
    }
    if (argc)
    {
        post("warning: array define ignoring extra argument: ");
        postatom(argc, argv);
    }
    x = (t_glist *)table_donew(arrayname, arraysize, keep);
    
        /* bash the class to "array define".  We don't do this earlier in
        part so that canvas_getcurrent() will work while the glist and
        garray are being created.  There may be other, unknown side effects. */
    x->gl_obj.ob_pd = array_define_class;
    return (x);
}

void garray_savecontentsto(t_garray *x, t_binbuf *b);

void array_define_save(t_gobj *z, t_binbuf *bb)
{
    t_glist *x = (t_glist *)z;
    t_glist *gl = (x->gl_list ? pd_checkglist(&x->gl_list->g_pd) : 0);
    binbuf_addv(bb, "ssff", &s__X, gensym("obj"),
        (float)x->gl_obj.te_xpix, (float)x->gl_obj.te_ypix);
    binbuf_addbinbuf(bb, x->gl_obj.ob_binbuf);
    binbuf_addsemi(bb);

    garray_savecontentsto((t_garray *)gl->gl_list, bb);
}

t_scalar *garray_getscalar(t_garray *x);

    /* send a pointer to the scalar that owns this array to
    whomever is bound to the given symbol */
static void array_define_send(t_glist *x, t_symbol *s)
{
    t_glist *gl = (x->gl_list ? pd_checkglist(&x->gl_list->g_pd) : 0);
    if (!s->s_thing)
        pd_error(x, "array_define_send: %s: no such object", s->s_name);
    else if (gl && gl->gl_list && pd_class(&gl->gl_list->g_pd) == garray_class)
    {
        t_gpointer gp;
        gpointer_init(&gp);
        gpointer_setglist(&gp, gl,
            garray_getscalar((t_garray *)gl->gl_list));
        pd_pointer(s->s_thing, &gp);
        gpointer_unset(&gp);
    }
    else bug("array_define_anything");
}

    /* just forward any messages to the garray */
static void array_define_anything(t_glist *x,
    t_symbol *s, int argc, t_atom *argv)
{
    t_glist *gl = (x->gl_list ? pd_checkglist(&x->gl_list->g_pd) : 0);
    if (gl && gl->gl_list && pd_class(&gl->gl_list->g_pd) == garray_class)
        typedmess(&gl->gl_list->g_pd, s, argc, argv);
    else bug("array_define_anything");
}


/* ---  array_client - common code for objects that refer to arrays -- */

typedef struct _array_client
{
    t_object tc_obj;
    t_symbol *tc_sym;
    t_gpointer tc_gp;
    t_symbol *tc_struct;
    t_symbol *tc_field;
    t_canvas *tc_canvas;
} t_array_client;

#define x_sym x_tc.tc_sym
#define x_struct x_tc.tc_struct
#define x_field x_tc.tc_field
#define x_gp x_tc.tc_gp

    /* find the array for this object.  Prints an error  message and returns
        0 on failure. */
static t_array *array_client_getbuf(t_array_client *x, t_glist **glist)
{
    if (x->tc_sym)       /* named array object */
    {
        t_garray *y = (t_garray *)pd_findbyclass(x->tc_sym, garray_class);
        if (y)
        {
            *glist = garray_getglist(y);
            return (garray_getarray(y));
        }
        else
        {
            pd_error(x, "array: couldn't find named array '%s'",
                x->tc_sym->s_name);
            *glist = 0;
            return (0);
        }
    }
    else if (x->tc_struct)   /* by pointer */
    {
        t_template *template = template_findbyname(x->tc_struct);
        t_gstub *gs = x->tc_gp.gp_stub;
        t_word *vec; 
        int onset, type;
        t_symbol *arraytype;
        if (!template)
        {
            pd_error(x, "array: couldn't find struct %s", x->tc_struct->s_name);
            return (0);
        }
        if (!gpointer_check(&x->tc_gp, 0))
        {
            pd_error(x, "array: stale or empty pointer");
            return (0);
        }
        if (gs->gs_which == GP_ARRAY)
            vec = x->tc_gp.gp_un.gp_w;
        else vec = x->tc_gp.gp_un.gp_scalar->sc_vec;

        if (!template_find_field(template,
            x->tc_field, &onset, &type, &arraytype))
        {
            pd_error(x, "array: no field named %s", x->tc_field->s_name);
            return (0);
        }
        if (type != DT_ARRAY)
        {
            pd_error(x, "array: field %s not of type array",
                x->tc_field->s_name);
            return (0);
        }
        if (gs->gs_which == GP_GLIST)
            *glist = gs->gs_un.gs_glist;
        else
        {
            t_array *owner_array = gs->gs_un.gs_array;
            while (owner_array->a_gp.gp_stub->gs_which == GP_ARRAY)
                owner_array = owner_array->a_gp.gp_stub->gs_un.gs_array;
            *glist = owner_array->a_gp.gp_stub->gs_un.gs_glist;
        }
        return (*(t_array **)(((char *)vec) + onset));
    }
    else return (0);    /* shouldn't happen */
}

static  void array_client_senditup(t_array_client *x)
{
    t_glist *glist;
    t_array *a = array_client_getbuf(x, &glist);
    array_redraw(a, glist);
}

static void array_client_free(t_array_client *x)
{
    gpointer_unset(&x->tc_gp);
}

/* ----------  array size : get or set size of an array ---------------- */
static t_class *array_size_class;

typedef struct _array_size
{
    t_array_client x_tc;
} t_array_size;
#define x_outlet x_tc.tc_obj.ob_outlet

static void *array_size_new(t_symbol *s, int argc, t_atom *argv)
{
    t_array_size *x = (t_array_size *)pd_new(array_size_class);
    x->x_sym = x->x_struct = x->x_field = 0;
    gpointer_init(&x->x_gp);
    while (argc && argv->a_type == A_SYMBOL &&
        *argv->a_w.w_symbol->s_name == '-')
    {
        if (!strcmp(argv->a_w.w_symbol->s_name, "-s") &&
            argc >= 3 && argv[1].a_type == A_SYMBOL &&
                argv[2].a_type == A_SYMBOL)
        {
            x->x_struct = canvas_makebindsym(argv[1].a_w.w_symbol);
            x->x_field = argv[2].a_w.w_symbol;
            argc -= 2; argv += 2;
        }
        else
        {
            pd_error(x, "array setline: unknown flag ...");
            postatom(argc, argv);
        }
        argc--; argv++;
    }
    if (argc && argv->a_type == A_SYMBOL)
    {
        if (x->x_struct)
        {
            pd_error(x, "array setline: extra names after -s..");
            postatom(argc, argv);
        }
        else x->x_sym = argv->a_w.w_symbol;
        argc--; argv++;
    }
    if (argc)
    {
        post("warning: array setline ignoring extra argument: ");
        postatom(argc, argv);
    }
    if (x->x_struct)
        pointerinlet_new(&x->x_tc.tc_obj, &x->x_gp);
    outlet_new(&x->x_tc.tc_obj, &s_float);
    return (x);
}

static void array_size_bang(t_array_size *x)
{
    t_glist *glist;
    t_array *a = array_client_getbuf(&x->x_tc, &glist);
    if (a)
        outlet_float(x->x_outlet, a->a_n);
}

static void array_size_float(t_array_size *x, t_floatarg f)
{
    t_glist *glist;
    t_array *a = array_client_getbuf(&x->x_tc, &glist);
    if (a)
    {
              /* if it's a named array object we have to go back and find the
              garray (repeating work done in array_client_getbuf()) because
              the garray might want to adjust.  Maybe array_client_getbuf
              should have a return slot for the garray if any?  */
        if (x->x_tc.tc_sym)
        {
            t_garray *y = (t_garray *)pd_findbyclass(x->x_tc.tc_sym,
                garray_class);
            garray_resize(y, f);
        }
        else
        {
            int n = f;
            if (n < 1)
                n = 1;
             array_resize_and_redraw(a, glist, n);
        }
    }
}

/* --------  array sum : get sum of all or a range in an array -------- */
static t_class *array_sum_class;

typedef struct _array_rangeop   /* any operation meaningful on a subrange */
{
    t_array_client x_tc;
    float x_onset;
    float x_n;
    t_symbol *x_elemfield;
    t_symbol *x_elemtemplate;   /* unused - perhaps should at least check it */
} t_array_rangeop;

#define t_array_sum t_array_rangeop

static void *array_sum_new(t_symbol *s, int argc, t_atom *argv)
{
    t_array_rangeop *x = (t_array_rangeop *)pd_new(array_sum_class);
    x->x_sym = x->x_struct = x->x_field = 0;
    gpointer_init(&x->x_gp);
    x->x_elemtemplate = &s_;
    x->x_elemfield = gensym("y"); 
    x->x_onset = 0;
    x->x_n = -1;
    floatinlet_new(&x->x_tc.tc_obj, &x->x_n);
    while (argc && argv->a_type == A_SYMBOL &&
        *argv->a_w.w_symbol->s_name == '-')
    {
        if (!strcmp(argv->a_w.w_symbol->s_name, "-s") &&
            argc >= 3 && argv[1].a_type == A_SYMBOL &&
                argv[2].a_type == A_SYMBOL)
        {
            x->x_struct = canvas_makebindsym(argv[1].a_w.w_symbol);
            x->x_field = argv[2].a_w.w_symbol;
            argc -= 2; argv += 2;
        }
        else if (!strcmp(argv->a_w.w_symbol->s_name, "-s") &&
            argc >= 3 && argv[1].a_type == A_SYMBOL &&
                argv[2].a_type == A_SYMBOL)
        {
            x->x_elemtemplate = argv[1].a_w.w_symbol;
            x->x_elemfield = argv[2].a_w.w_symbol;
            argc -= 2; argv += 2;
        }
        else
        {
            pd_error(x, "array setline: unknown flag ...");
            postatom(argc, argv);
        }
        argc--; argv++;
    }
    if (argc && argv->a_type == A_SYMBOL)
    {
        if (x->x_struct)
        {
            pd_error(x, "array setline: extra names after -s..");
            postatom(argc, argv);
        }
        else x->x_sym = argv->a_w.w_symbol;
        argc--; argv++;
    }
    if (argc && argv->a_type == A_FLOAT)
    {
        x->x_onset = argv->a_w.w_float;
        argc--; argv++;
    }
    if (argc && argv->a_type == A_FLOAT)
    {
        x->x_n = argv->a_w.w_float;
        argc--; argv++;
    }
    if (argc)
    {
        post("warning: array setline ignoring extra argument: ");
        postatom(argc, argv);
    }
    if (x->x_struct)
        pointerinlet_new(&x->x_tc.tc_obj, &x->x_gp);
    outlet_new(&x->x_tc.tc_obj, &s_float);
    return (x);
}

static void array_sum_bang(t_array_rangeop *x)
{
    t_glist *glist;
    t_array *a = array_client_getbuf(&x->x_tc, &glist);
    char *elemp;
    int stride, onset, firstitem, nitem, i, type;
    t_symbol *arraytype;
    double sum;
    t_template *template;
    if (!a)
        return;
    template = template_findbyname(a->a_templatesym);
    if (!template_find_field(template, x->x_elemfield, &onset,
        &type, &arraytype) || type != DT_FLOAT)
    {
        pd_error(x, "can't find field %s in struct %s",
            x->x_elemfield->s_name, a->a_templatesym->s_name);
        return;
    }
    stride = a->a_elemsize;
    firstitem = x->x_onset;
    if (firstitem < 0)
        firstitem = 0;
    else if (firstitem > a->a_n)
        firstitem = a->a_n;
    if (x->x_n < 0)
        nitem = a->a_n - firstitem;
    else
    {
        nitem = x->x_n;
        if (nitem + firstitem > a->a_n)
            nitem = a->a_n - firstitem;
    }
    for (i = 0, sum = 0, elemp = a->a_vec+onset+firstitem*stride; i < nitem;
        i++, elemp += stride)
            sum += *(t_float *)elemp;
    outlet_float(x->x_outlet, sum);
}

static void array_sum_float(t_array_rangeop *x, t_floatarg f)
{
    x->x_onset = f;
    array_sum_bang(x);
}

/* overall creator for "array" objects - dispatch to "array define" etc */
static void *arrayobj_new(t_symbol *s, int argc, t_atom *argv)
{
    if (!argc || argv[0].a_type != A_SYMBOL)
        newest = array_define_new(s, argc, argv);
    else
    {
        char *str = argv[0].a_w.w_symbol->s_name;
        if (!strcmp(str, "d") || !strcmp(str, "define"))
            newest = array_define_new(s, argc-1, argv+1);
        else if (!strcmp(str, "size"))
            newest = array_size_new(s, argc-1, argv+1);
        else if (!strcmp(str, "sum"))
            newest = array_sum_new(s, argc-1, argv+1);
        else 
        {
            error("array %s: unknown function", str);
            newest = 0;
        }
    }
    return (newest);
}

void canvas_add_for_class(t_class *c);

/* ---------------- global setup function -------------------- */

void x_array_setup(void )
{
    array_define_class = class_new(gensym("array define"), 0,
        (t_method)canvas_free, sizeof(t_canvas), 0, 0);
    canvas_add_for_class(array_define_class);
    class_addmethod(array_define_class, (t_method)array_define_send,
        gensym("send"), A_SYMBOL, 0);
    class_addanything(array_define_class, array_define_anything);
    class_sethelpsymbol(array_define_class, gensym("array-object"));
    class_setsavefn(array_define_class, array_define_save);

    class_addcreator((t_newmethod)arrayobj_new, gensym("array"), A_GIMME, 0);

    class_addcreator((t_newmethod)table_new, gensym("table"),
        A_DEFSYM, A_DEFFLOAT, 0);

    array_size_class = class_new(gensym("array size"),
        (t_newmethod)array_size_new, (t_method)array_client_free,
            sizeof(t_array_size), 0, A_GIMME, 0);
    class_addbang(array_size_class, array_size_bang);
    class_addfloat(array_size_class, array_size_float);
    class_sethelpsymbol(array_size_class, gensym("array-object"));

    array_sum_class = class_new(gensym("array sum"),
        (t_newmethod)array_sum_new, (t_method)array_client_free,
            sizeof(t_array_sum), 0, A_GIMME, 0);
    class_addbang(array_sum_class, array_sum_bang);
    class_addfloat(array_sum_class, array_sum_float);
    class_sethelpsymbol(array_sum_class, gensym("array-object"));
}