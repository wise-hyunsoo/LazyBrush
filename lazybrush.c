#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <Python.h>
#include <numpy/arrayobject.h>
#include "mincut.c"


npy_intp explore_regions(Node *points, npy_intp length, char *colornames, 
			 Node **colorsets, npy_intp mincol, npy_intp numcols) {

  printf("EXPLORE\n");
  npy_intp k;
  Node *p, *q;
  for(k=0; k<length; k++) {
    p = points+k;
    if(p->disabled || p->label) continue;

    // Exlore la région à la recherche de plusieurs couleurs
    int color = dfs(p, 1, 0);
    printf(" Couleur %d\n", color);

    // Si une seule -> on colorie la région et on désactive les points
    if(color) {
      printf("Région monochrome %d\n", (int)color);
      dfs(p, 2, color);

      // On cherche l'existence de cette couleur dans d'autres régions actives
      char stillalive = 0;
      for(q = colorsets[color-1]; q != NULL; q = q->_next_in_group)
	if(!q->disabled) {
	  stillalive = 1;
	  break;
	}

      // Si non : on supprime la couleur
      if(!stillalive) {
	printf("Suppression de cette couleur\n");
	int ci = mincol;
	for(; colornames[ci] != color && ci<numcols; ci++){}
	colornames[ci] = colornames[mincol];
	colornames[mincol] = color;
	mincol++;
      }
    }
  }
  return mincol;
}

static PyObject *
lazybrush_wrapper(PyObject *self, PyObject *args)
{

  // Lecture des paramètres
  PyArrayObject *sketch=NULL, *colors=NULL, *colorlist=NULL, *output=NULL;
  float K, lambda;

  if (!PyArg_ParseTuple(args, "O!O!O!ffO!", 
			&PyArray_Type, &sketch, &PyArray_Type, &colors, 
			&PyArray_Type, &colorlist,
			&K, &lambda, 
			&PyArray_Type, &output)) return NULL;

  int s_nd = PyArray_NDIM(sketch), c_nd = PyArray_NDIM(colors), 
    o_nd = PyArray_NDIM(output), cl_nd = PyArray_NDIM(colorlist);
  npy_intp *s_dims = PyArray_DIMS(sketch), *c_dims = PyArray_DIMS(colors), *o_dims = PyArray_DIMS(output);
  npy_intp wdt = s_dims[0], hgt = s_dims[1];

  if (s_nd != 2 || c_nd != 1 || o_nd != 1 || cl_nd != 1
      || c_dims[0] != wdt * hgt || o_dims[0] != c_dims[0]) {
    PyErr_SetString(PyExc_ValueError,
		    "Paramètres invalides");
    return NULL;
  }

  // Initialisation des structures de graphe
  printf("Dimensions : %dx%d, K : %f\n", (int)wdt, (int)hgt, K);

  Graph *graph = make_graph();
  Node *points = spawn_nodes(graph, wdt*hgt);
  Node *points_end = points + wdt*hgt;

  npy_intp i, j;
  npy_intp numcols = PyArray_DIMS(colorlist)[0];

  Node **colorsets = (Node **)malloc((numcols-1)*sizeof(Node *));
  char *colornames = (char *)malloc(numcols*sizeof(char));
  colornames[0] = 0; // Transparent -> ne doit pas rester à la fin
  for(i=0;i<numcols-1;i++){
    colorsets[i] = NULL;
    colornames[i+1] = numcols-i-1;
  }
  

  for(j=0; j<hgt; j++) {
    for(i=0; i<wdt; i++) {
      Node *p = points+i+j*wdt, *q;
      Edge *e;
      float cp = *((double *)PyArray_GETPTR2(sketch, i, j));
      if(i==wdt/2) {
	printf("%f ", cp);
      }
      if (i < wdt-1) {
	// cq = *((double *)PyArray_GETPTR2(sketch, i+1, j));
	q = p+1;
	e = connect_nodes(p, q, K * cp + 1.0f, NULL);
	connect_nodes(q, p, K * cp + 1.0f, e);
      }
      if (j < hgt-1) {
	q = p+wdt;
	// cq = *((double *)PyArray_GETPTR2(sketch, i, j+1));
	e = connect_nodes(p, q, K * cp + 1.0f, NULL);
	connect_nodes(q, p, K * cp + 1.0f, e);
      }
      npy_intp c = *((npy_intp *)PyArray_GETPTR1(colors, i+j*wdt));
      if(c>0) {
	p->_next_in_group = colorsets[c-1];
	p->color = c;
	colorsets[c-1] = p;
      }
    }
  }
 

  npy_intp c = 1, k;
  char color = 1;
  Node *s = spawn_nodes(graph, 2);
  Node *t = s+1;
  s->origin = t->origin = 1;
  Node *p;

  // Algorithme
  printf("%d Couleurs\n", (int)numcols);
  while(c < numcols) {
    // Exploration des régions à la recherche de monochromes
    c = explore_regions(points, wdt*hgt, colornames, 
			    colorsets, c, numcols);
    color = colornames[c];

    if(c >= numcols-1) break;
    // Fabrication du graphe
    printf("Traitement couleur %d (%d) contre", (int)c, (int)color);
    // On relie les points de couleur color à S
    int n = 0;
    for (p = colorsets[color-1]; p != NULL; p = p->_next_in_group) {
      if(p->disabled) continue;
      n++;
      Edge *e = connect_nodes(s, p, (1.0f-lambda)*K, NULL);
      connect_nodes(p, s, 0.0f, e);
    }
    if (n==0) { // Pas de pixel actif de cette couleur
      printf("Pas de pixel actif\n");
      c++;
      continue;
    }
    int d;
    npy_intp acolor;
    // On relie les points de couleur non encore traitée à T
    for (d = c+1; d<numcols; d++) {
      acolor = colornames[d];
      printf(" %d", (int)acolor);
      for(p = colorsets[acolor-1]; p != NULL; p = p->_next_in_group) {
	if(p->disabled) continue;
	Edge *e = connect_nodes(p, t, (1.0f-lambda)*K, NULL);
	connect_nodes(t, p, 0.0f, e);
      }
    }
    printf("\n");

    // Mincut
    mincut(graph, s, t);

    printf("\n\n");
    // On assigne la couleur à ce qui a été relié à S
    for(k = 0; k < wdt*hgt; k++) {
      p = points+k;
      if(p->disabled) continue;
      if(p->tree == -1) {
	p->color = color;
	disable_node(p);
      }// else printf("%d", p->tree);
      if(p->tree == 0) { // just debug
	p->color = 0;
	disable_node(p);
      }
    }
    
    // On efface la couleur des résidus
    for (p = colorsets[color-1]; p != NULL; p = p->_next_in_group) {
      if(p->disabled) continue;
      p->color = 0;
    }

    // On réinitialise le graphe
    disable_node(s);
    disable_node(t);
    for(p = points; p < points_end; p++)
      if(!(p->disabled)) full_reinit_node(p);
    reinit_node(s);
    reinit_node(t);
    enable_node(s); 
    enable_node(t);
    // On désactive la couleur
    c++;
  }

  for(k = 0; k < wdt*hgt; k++) {
    p = points+k;
    *((npy_intp *)PyArray_GETPTR1(output, k)) = p->color;
  }
  

  // Nettoyage
  free(colorsets);
  free(colornames);
  free(s);
  free(graph);
  free(points);

  Py_INCREF(Py_None);
  return Py_None;

  //fail:
  //return NULL;
}

static PyMethodDef LazybrushMethods[] = {
  {"wrapper", lazybrush_wrapper, METH_VARARGS,
   "Computes lazybrush"},
  {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef lazybrushmodule = {
    PyModuleDef_HEAD_INIT,
    "lazybrush",   /* name of module */
    "Computes lazybrush algorithm", /* module documentation, may be NULL */
    -1,       /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
    LazybrushMethods
};

PyMODINIT_FUNC
PyInit_lazybrush(void)
{
    PyObject *module = PyModule_Create(&lazybrushmodule);
    if (module == NULL) {
        return NULL;
    }

    // Initialize NumPy
    import_array();
    if (PyErr_Occurred()) {
        Py_DECREF(module);
        return NULL;
    }

    return module;
}

