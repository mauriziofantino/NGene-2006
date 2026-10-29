/**
 * @file matrices.h
 *
 * @author Mario Nicola
 *
 * @date 23/10/2014
 *
 * @version 1.0.0
 *
 * @brief Functions used to manage matrices.
 */

#ifndef MATRICES_H_INCLUDED
#define MATRICES_H_INCLUDED

/* C storage adapters implemented by Eigen in pvt/pntNumerics.cpp. */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the matrix to zero
 *
 * @param m is the matrix
 * @param rows is the rows number
 * @param cols id the columns number
 */
void MatrixInit(double *m, int rows, int cols);


/**
 * @brief Performs product (rows per columns) between two matrices
 *
 * @param input1 is the pointer to matrix 1
 * @param rows1 number of rows of matrix 1
 * @param columns1 number of columns of matrix 1
 * @param input2 is the pointer to matrix 2
 * @param rows2 number of rows of matrix 2
 * @param columns2 number of columns of matrix 2
 * @param output  = input1 x input2 (return value)
 */
void MatrixProduct(double *input1,int rows1,int columns1,double *input2,int rows2,int columns2,double *output);


/**
 * @brief Performs the matrix transpose
 *
 * @param input is the pointer to matrix
 * @param rows indicates number of rows
 * @param columns indicates number of columns
 * @param output transposed matrix
 */
void MatrixTrasp(double *input, int rows, int columns, double *output);


/**
 * @brief Performs the sum between two matrices
 *
 * @param input1 is the pointer to matrix 1
 * @param rows1 number of rows of matrix 1
 * @param columns1 number of columns of matrix 1
 * @param input2 is the pointer to matrix 2
 * @param rows2 number of rows of matrix 2
 * @param columns2 number of columns of matrix 2
 * @param output  = input1 + input2 (return value)
 */
void MatrixSum(double *input1, int rows1, int columns1, double *input2, int rows2, int columns2, double *output);


/**
 * @brief Performs the difference between two matrices
 *
 * @param input1 is the pointer to matrix 1
 * @param rows1 number of rows of matrix 1
 * @param columns1 number of columns of matrix 1
 * @param input2 is the pointer to matrix 2
 * @param rows2 number of rows of matrix 2
 * @param columns2 number of columns of matrix 2
 * @param output  = input1 - input2 (return value)
 */
void MatrixDiff(double *input1, int rows1, int columns1, double *input2, int rows2, int columns2, double *output);


/**
 * @brief Merges vertically the matrices input1 and input2 in the output matrix output
 *
 * @param input1 is the pointer to matrix 1
 * @param rows1 number of rows of matrix 1
 * @param columns1 number of columns of matrix 1
 * @param input2 is the pointer to matrix 2
 * @param rows2 number of rows of matrix 2
 * @param columns2 number of columns of matrix 2
 * @param output is the output matrix
 */
void MergeMatricesVertically(double *input1, int rows1, int columns1, double *input2, int rows2, int columns2, double *output);

/**
 * @brief Builds an identity matrix
 *
 * @param output is identity matrix (return value)
 * @param size is the matrix dimension
 */
void MatrixIdentity(double *output, int size);


#ifdef __cplusplus
}
#endif
#endif /* MATRICES_H_INCLUDED */
