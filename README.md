## holmes
It's elementary.

---
### 0. Code block diagram 
<img src="http://www.math.vt.edu/people/tcew/libParanumalDiagramLocal-crop-V2.png" width="600" >

---
### 1. Clone: Holmes
`git clone https://github.com/tcew/holmes`

---
### 2. OCCA dependency (currently OCCA 1.0 forked by Noel Chalmers) 
`git clone https://github.com/noelchalmers/occa`

#### 2-1. Build OCCA 
`cd occa`    
export OCCA_DIR=\`pwd\`  
`export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$OCCA_DIR/lib  
make -j  
cd ../  `  

---
### 3. Running the codes: 

The elliptic solver and flow solvers reside in sub-directories of the solver directory. Each sub-directory includes makefile, src directory, data directory (including header files for defining boundary conditions), okl kernel directory, and setups directory. The setups directory includes a number of example input files that specify input parameters for the solver.

#### 3-1. Build holmes elliptic example
  
`cd holmes/solvers/elliptic  
make -j  `

#### 3-2. Run elliptic example with provided quadrilateral set up file on a single device:
  
`./ellipticMain setups/setupQuad2D.rc`  

#### 3-3. Run the same example with two devices:

`mpiexec -n 2 ./ellipticMain setups/setupQuad2D.rc`  
 
---

### 4. Directory structure:

The directory structure is relatively flat, annotated here:

holmes/  
├── 3rdParty  
│   ├── BlasLapack  
│   └── gslib.github  
├── meshes  
├── nodes           `(Node data files for different elements)`  
├── okl  
├── solvers  
│   ├── acoustics   `(DGTD discretization of linearized Euler acoustics)`  
│   │   ├── okl     `(OCCA Kernel Language DEVICE kernels for acoustics)`  
│   │   ├── setups  `(Default set up files for acoustics solver)`    
│   │   └── src     `(HOST code for driving acoustics solver)`  
│   ├── bns         `(DGTD discretization of Galerkin-Boltzmann gas dynamics solver)`  
│   │   ├── data  
│   │   ├── okl  
│   │   ├── setups  
│   │   └── src  
│   ├── cns         `(DGTD discretization based isothermal compressible Navier-Stokes solver)`  
│   │   ├── data  
│   │   ├── okl  
│   │   ├── setups  
│   │   └── src  
│   ├── elliptic    `(DG and C0-FEM discretization based Poisson and screened Poisson potential problems)`    
│   │   ├── data  
│   │   ├── okl  
│   │   ├── setups  
│   │   └── src  
│   ├── gradient    `(Elemental gradient operations example)`  
│   │   ├── okl  
│   │   ├── setups  
│   │   └── src  
│   ├── ins         `(DG and C0-FEM discretization based incompressible Navier-Stokes solver)`   
│   │   ├── data  
│   │   ├── okl  
│   │   ├── setups  
│   │   └── src  
│   └── parALMOND   `(Hybrid p-type multigrid and algebraic multigrid linear solvers)`  
│       ├── include  
│       ├── okl  
│       └── src  
├── src             `(Base library for mesh wrangling)`  
└── utilities       `(Useful utilities)`  
    ├── autoTester  
    └── VTU  

