# -*- Makefile -*-
#
# Makefile for DynEarthSol3D
#
# Author: Eh Tan <tan2@earth.sinica.edu.tw>
#

## Execute "make" if making production run. Or "make opt=0 openmp=0" for debugging run.
##
## ndims = 3: 3D code; 2: 2D code
## opt = 1 ~ 3: optimized build; others: debugging build
## openmp = 1: enable OpenMP
## useadapt = 1: use libadaptivity for mesh optimization during remeshing

ndims = 3
opt = 2
openmp = 1
useadapt = 1

## Select C++ compiler
ifeq ($(useadapt), 1)
	CXX = mpicxx # g++-mp-4.7

	# path to vtk header files
	VTK_INCDIR = /opt/local/include/vtk-5.10

	# flag to link with fortran binding of MPI library
	LIB_MPIFORTRAN = -lmpi_mpifh # OpenMPI 1.10.2. Other possibilities: -lmpifort, -lfmpich, -lmpi_f77
else
	CXX = g++
endif

## Boost location and library name
BOOST_ROOT_DIR = ${HOME}/opt/boost_1_55_0

########################################################################
## Select compiler and linker flags
## (Usually you won't need to modify anything below)
########################################################################

BOOST_LDFLAGS = -lboost_program_options
ifdef BOOST_ROOT_DIR
	# check existence of stage/ directory
	has_stage_dir = $(wildcard $(BOOST_ROOT_DIR)/stage)
	ifeq (, $(has_stage_dir))
		# no stage dir, BOOST_ROOT_DIR is the installation directory
		BOOST_CXXFLAGS = -I$(BOOST_ROOT_DIR)/include
		BOOST_LDFLAGS += -L$(BOOST_ROOT_DIR)/lib -Wl,-rpath=$(BOOST_ROOT_DIR)/lib
	else
		# with stage dir, BOOST_ROOT_DIR is the build directory
		BOOST_CXXFLAGS = -I$(BOOST_ROOT_DIR)
		BOOST_LDFLAGS += -L$(BOOST_ROOT_DIR)/stage/lib #-Wl,-rpath=$(BOOST_ROOT_DIR)/stage/lib
	endif
endif

ifneq (, $(findstring mpicxx, $(CXX))) # if using any version of g++
	CXXFLAGS = -g -std=c++0x
	LDFLAGS = -lm

	ifeq ($(opt), 1)
		CXXFLAGS += -O1
	else ifeq ($(opt), 2)
		CXXFLAGS += -O2
	else ifeq ($(opt), 3) # experimental, use at your own risk :)
		CXXFLAGS += -march=native -O3 -ffast-math -funroll-loops
	else # debugging flags
		CXXFLAGS += -O0 -Wall -Wno-unused-variable -Wno-unused-function -Wno-unknown-pragmas -fbounds-check -ftrapv
	endif

	ifeq ($(openmp), 1)
		CXXFLAGS += -fopenmp -DUSE_OMP
		LDFLAGS += -fopenmp
	endif

else ifneq (, $(findstring icpc, $(CXX))) # if using intel compiler, tested with v14
		CXXFLAGS = -g -std=c++0x
	        LDFLAGS = -lm

	        ifeq ($(opt), 1)
	                CXXFLAGS += -O1
	        else ifeq ($(opt), 2)
	                CXXFLAGS += -O2
	        else ifeq ($(opt), 3) # experimental, use at your own risk :)
	                CXXFLAGS += -fast -fast-transcendentals -fp-model fast=2
	        else # debugging flags
	                CXXFLAGS += -O0 -check=uninit -check-pointers=rw -check-pointers-dangling=all -fp-trap-all=all
	        endif

	        ifeq ($(openmp), 1)
	                CXXFLAGS += -fopenmp -DUSE_OMP
	                LDFLAGS += -fopenmp
	        endif



else
# the only way to display the error message in Makefile ...
all:
	@echo "Unknown compiler, check the definition of 'CXX' in the Makefile."
	@false
endif

## Is this a mercurial repository?
HAS_HG := $(shell hg log -r tip --template '{node}' 2>/dev/null)

##

SRCS =	\
	barycentric-fn.cxx \
	brc-interpolation.cxx \
	bc.cxx \
	binaryio.cxx \
	dynearthsol.cxx \
	fields.cxx \
	geometry.cxx \
	ic.cxx \
	ic-read-temp.cxx \
	input.cxx \
	matprops.cxx \
	mesh.cxx \
	nn-interpolation.cxx \
	output.cxx \
	phasechanges.cxx \
	remeshing.cxx \
	rheology.cxx \
	markerset.cxx

INCS =	\
	array2d.hpp \
	barycentric-fn.hpp \
	binaryio.hpp \
	constants.hpp \
	parameters.hpp \
	matprops.hpp \
	sortindex.hpp \
	utils.hpp \
	mesh.hpp \
	markerset.hpp \
	output.hpp

OBJS = $(SRCS:.cxx=.$(ndims)d.o)

EXE = dynearthsol$(ndims)d


## Libraries

TET_SRCS = tetgen/predicates.cxx tetgen/tetgen.cxx
TET_INCS = tetgen/tetgen.h
TET_OBJS = $(TET_SRCS:.cxx=.o)

TRI_SRCS = triangle/triangle.c
TRI_INCS = triangle/triangle.h
TRI_OBJS = $(TRI_SRCS:.c=.o)

M_SRCS = $(TRI_SRCS)
M_INCS = $(TRI_INCS)
M_OBJS = $(TRI_OBJS)

ifeq ($(ndims), 3)
	M_SRCS += $(TET_SRCS)
	M_INCS += $(TET_INCS)
	M_OBJS += $(TET_OBJS)
	CXXFLAGS += -DTHREED
endif

C3X3_DIR = 3x3-C
C3X3_LIBNAME = 3x3

ANN_DIR = ann
ANN_LIBNAME = ANN
CXXFLAGS += -I$(ANN_DIR)/include

ifeq ($(useadapt), 1)
	LIBADAPTIVITY_DIR = ./libadaptivity
	LIBADAPTIVITY_INC = $(LIBADAPTIVITY_DIR)/include
	LIBADAPTIVITY_LIB = $(LIBADAPTIVITY_DIR)/lib
	LIBADAPTIVITY_LIBNAME = adaptivity
	CXXFLAGS += -I$(LIBADAPTIVITY_INC) -I$(VTK_INCDIR) -DADAPT -DHAVE_VTK=1 \
		-I$(LIBADAPTIVITY_DIR)/adapt3d/include -I$(LIBADAPTIVITY_DIR)/metric_field/include \
		-I$(LIBADAPTIVITY_DIR)/load_balance/include
endif

## Action

.PHONY: all clean take-snapshot

all: $(EXE) take-snapshot

ifeq ($(useadapt), 1)

$(LIBADAPTIVITY_DIR)/lflags.mk:
	@grep '^LFLAGS' $(LIBADAPTIVITY_DIR)/adapt3d/Makefile > $@

$(LIBADAPTIVITY_DIR)/Makefile: $(LIBADAPTIVITY_DIR)/configure
	@cd $(LIBADAPTIVITY_DIR) && ./configure

$(LIBADAPTIVITY_LIB)/libadaptivity.a: $(LIBADAPTIVITY_DIR)/Makefile $(LIBADAPTIVITY_DIR)/lflags.mk
	@+$(MAKE) -C $(LIBADAPTIVITY_DIR)

-include $(LIBADAPTIVITY_DIR)/lflags.mk
LIBADAPTIVITY_LIBS = $(LIBADAPTIVITY_LIB)/libadaptivity.a $(LFLAGS) $(LIB_MPIFORTRAN)

$(EXE): $(M_OBJS) $(OBJS) $(C3X3_DIR)/lib$(C3X3_LIBNAME).a $(ANN_DIR)/lib/lib$(ANN_LIBNAME).a $(LIBADAPTIVITY_LIB)/libadaptivity.a
		$(CXX) $(M_OBJS) $(OBJS) $(LDFLAGS) $(BOOST_LDFLAGS) \
			-L$(C3X3_DIR) -l$(C3X3_LIBNAME) -L$(ANN_DIR)/lib -l$(ANN_LIBNAME) \
			$(LIBADAPTIVITY_LIBS) \
			-o $@
else
$(EXE): $(M_OBJS) $(OBJS) $(C3X3_DIR)/lib$(C3X3_LIBNAME).a $(ANN_DIR)/lib/lib$(ANN_LIBNAME).a
		$(CXX) $(M_OBJS) $(OBJS) $(LDFLAGS) $(BOOST_LDFLAGS) \
			-L$(C3X3_DIR) -l$(C3X3_LIBNAME) -L$(ANN_DIR)/lib -l$(ANN_LIBNAME) \
			-o $@
endif

take-snapshot:
	@# snapshot of the code for building the executable
	@echo Flags used to compile the code: > snapshot.diff
	@echo '  '  CXX=$(CXX) opt=$(opt) openmp=$(openmp) >> snapshot.diff
	@echo '  '  PATH=$(PATH) >> snapshot.diff
	@echo '  '  LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) >> snapshot.diff
ifneq ($(HAS_HG),)
	@echo >> snapshot.diff
	@echo >> snapshot.diff
	@echo '==== Summary of the code ====' >> snapshot.diff
	@hg summary >> snapshot.diff
	@echo >> snapshot.diff
	@echo >> snapshot.diff
	@echo '== Code modification (not checked-in) ==' >> snapshot.diff
	@hg diff >> snapshot.diff
	@hg log --patch -r "draft()" >> snapshot.diff
else
	@echo \'hg\' is not in path, cannot take code snapshot. >> snapshot.diff
endif

$(OBJS): %.$(ndims)d.o : %.cxx $(INCS)
	$(CXX) $(CXXFLAGS) $(BOOST_CXXFLAGS) -c $< -o $@

$(TRI_OBJS): %.o : %.c $(TRI_INCS)
	@# Triangle cannot be compiled with -O2
	$(CXX) $(CXXFLAGS) -O1 -DTRILIBRARY -DREDUCED -DANSI_DECLARATORS -c $< -o $@

tetgen/predicates.o: tetgen/predicates.cxx $(TET_INCS)
	@# Compiling J. Shewchuk predicates, should always be
	@# equal to -O0 (no optimization). Otherwise, TetGen may not
	@# work properly.
	$(CXX) $(CXXFLAGS) -DTETLIBRARY -O0 -c $< -o $@

tetgen/tetgen.o: tetgen/tetgen.cxx $(TET_INCS)
	$(CXX) $(CXXFLAGS) -DNDEBUG -DTETLIBRARY -Wno-unused-but-set-variable -Wno-int-to-pointer-cast -c $< -o $@

$(C3X3_DIR)/lib$(C3X3_LIBNAME).a:
	@+$(MAKE) -C $(C3X3_DIR)

$(ANN_DIR)/lib/lib$(ANN_LIBNAME).a:
	@+$(MAKE) -C $(ANN_DIR) linux-g++

deepclean: cleanadapt
	@rm -f $(TET_OBJS) $(TRI_OBJS) $(OBJS) $(EXE)
	@+$(MAKE) -C $(C3X3_DIR) clean

clean:
	@rm -f $(OBJS) $(EXE)

cleanadapt:
	@rm -f $(LIBADAPTIVITY_DIR)/lflags.mk
	@+$(MAKE) -C $(LIBADAPTIVITY_DIR) clean
