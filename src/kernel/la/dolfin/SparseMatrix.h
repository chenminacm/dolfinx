// Copyright (C) 2002 Johan Hoffman and Anders Logg.
// Licensed under the GNU GPL Version 2.

#ifndef __SPARSE_MATRIX_HH
#define __SPARSE_MATRIX_HH

#include <iostream>

#include <dolfin/constants.h>

namespace dolfin {
  
  class DenseMatrix;
  class Vector;
  
  class SparseMatrix {
  public:
	 
	 SparseMatrix  ();
	 SparseMatrix  (int m, int n);
	 ~SparseMatrix ();

	 // Enable different indexing for copy and write (thanks Jim)
	 class Reference {
	 public:

		Reference(SparseMatrix *sparseMatrix, int i, int j)
		{
		  this->sparseMatrix = sparseMatrix;
		  this->i = i;
		  this->j = j;
		}
		
		operator real() const {
		  return sparseMatrix->readElement(i,j);
		}

		void operator= (real value) {
		  sparseMatrix->writeElement(i,j,value);
		}

		void operator+= (real value) {
		  sparseMatrix->addtoElement(i,j,value);
		}
		
	 protected:
		
		SparseMatrix *sparseMatrix;
		int i;
		int j;

	 };

	 friend class Reference;

	 // Operators
	 void operator= (real a);
	 
	 /// Resize to empty matrix of given size
	 void init(int m, int n);
	 /// Resize matrix (clear unused elements)
	 void resize();
	 /// Clear matrix
	 void clear();
	 /// Returns size (0 for rows, 1 for columns)
	 int size(int dim);
	 /// Returns number of nonzero elements
	 int size();
	 /// Returns size of matrix in bytes (approximately)
	 int bytes();
	 /// Set number of nonzero entries in a row (clearing old values)
	 void initRow(int i, int rowsize);
	 /// Set number of nonzero entries in a row (keeping old values)
	 void resizeRow(int i, int rowsize);
	 /// Returns size of row i
	 int rowSize(int i);
	 
	 /// Indexing: fast alternative
	 real operator()(int i, int *j, int pos) const;
	 /// Indexing: slow alternative
	 Reference operator()(int i, int j);
		
	 /// Returns maximum norm
	 real norm();
	 /// Set all elements 0 on this row except (i,j) = 1
	 void setRowIdentity(int i);
	 /// Returns element i of Ax
	 real mult(Vector &x, int i);
	 /// Multiplies x with A and puts the result in Ax
	 void mult(Vector &x, Vector &Ax);

	 /// Output
	 void show();
	 friend ostream& operator << (ostream& output, SparseMatrix& sparseMatrix);

  protected:

	 real readElement  (int i, int j) const;
	 void writeElement (int i, int j, real value);
	 void addtoElement (int i, int j, real value);
	 
  private:

	 // Dimension
	 int m, n;

	 // Data
	 int  *rowsizes;
	 int  **columns;
	 real **values;

	 // Additional size to allocate when needed
	 int allocsize;
	 
  };

  typedef SparseMatrix Matrix;
  
}

#endif
