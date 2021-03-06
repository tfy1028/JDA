#ifndef DATA_HPP_
#define DATA_HPP_

#include <omp.h>
#include <vector>
#include <opencv2/core/core.hpp>

namespace jda {

// forward declaration
class Cart;
class DataSet;
class Feature;
class JoinCascador;

/*!
 * \brief similarity transform parameter
 */
class STParameter {
public:
  STParameter() : scale(1.) {
    rot[0][0] = 1.;
    rot[0][1] = 0.;
    rot[1][0] = 0.;
    rot[1][1] = 1.;
  }
  /*!
   * \brief calculate parameter between two shape, sR: shape2 -> shape1
   * \param shape1  shape1
   * \param shape2  shape2
   * \return        similarity transform paramter from shape2 to shape1
   */
  static STParameter Calc(const cv::Mat_<double>& shape1, const cv::Mat_<double>& shape2);
  /*!
   * \brief apply similarity transform, shape1 -> shape2, point1 -> point2
   * \param shape1    origin shape
   * \param shape2    transformed shape, should malloc the memory before call this function, can be shape1
   * \param x1, y1    origin point
   * \param x2, y2    transformed point
   */
  void Apply(const cv::Mat_<double>& shape1, cv::Mat_<double>& shape2) const;
  inline void Apply(double x1, double y1, double& x2, double& y2) const {
    x2 = scale*(rot[0][0] * x1 + rot[0][1] * y1);
    y2 = scale*(rot[1][0] * x1 + rot[1][1] * y1);
  }

public:
  double scale;
  double rot[2][2];
};

/*!
 * \brief Negative Training Sample Generator
 *  hard negative training sample will be needed if less negative alives
 */
class NegGenerator {
public:
  NegGenerator();
  ~NegGenerator();

public:
  /*!
   * \brief Generate more negative samples
   *  We will generate negative training samples from origin images, all generated samples
   *  should be hard enough to get through all stages of Join Cascador in current training
   *  state, it may be very hard to generate enough hard negative samples, we may fail with
   *  real size smaller than `int size`. We will give back all negative training samples with
   *  their scores and current shapes for further training.
   *
   * \note OpenMP supported hard negative mining, we may have `real size` > `size`
   *
   * \param join_cascador   JoinCascador in training
   * \param size            how many samples we need
   * \param imgs            negative samples
   * \param scores          scores of negative samples
   * \param shapes          shapes of samples, for training
   * \return                real size
   */
  int Generate(const JoinCascador& joincascador, int size, \
               std::vector<cv::Mat>& imgs, std::vector<double>& scores, \
               std::vector<cv::Mat_<double> >& shapes);
  /*!
   * \brief Load nagetive image file list from path
   * \param path    background image file list
   */
  void Load(const std::vector<std::string>& path);
  /*!
   * \brief Next image from bgs for hard mining
   *  using internal state to generator a patch from a bg or just from a prepared hard negative samples
   *
   * \note For parallel mining, NextImage should be thread safe.
   *
   * \param thread_id   thread id
   */
  cv::Mat NextImage(int thread_id);
  /*!
   * \brief Parallel hard negative mining
   *  mining hard negative in parallel, since `NegGenerator::NextImage()` is implemented in thread safe, we
   *  still need `write_lock` to put the mined negative sample into imgs, and calculate the statistic data.
   *
   * \param joincascador    JoinCascador in training
   * \param size            how many samples we need
   * \param imgs            negative samples
   * \param scores          scores of negative samples
   * \param shapes          shapes of samples, for training
   * \param write_lock      lock for `vector::push_back()`
   * \param nega_n          statistic data nega_n over threads
   * \param carts_n         statistic data carts_n over threads
   * \param ratio           statistic data mining process over threads
   */
  void ParallelMining(const JoinCascador& joincascador, int size, \
                      std::vector<cv::Mat>& img, std::vector<double>& scores, \
                      std::vector<cv::Mat_<double> >& shapes, \
                      omp_lock_t& write_lock, \
                      double& nega_n, double& carts_n, double& ratio);
  /*!
   * \brief Report how many background images have been used.
   * \note this function may not give the correct number in multi-thread mode, but shoud be roughly correct.
   *
   * \return    number of mined background images
   */
  int ReportBgImageUsed();

public:
  /*! \brief background image list */
  std::vector<std::string> list;
  /*! \brief hard negative list */
  std::vector<cv::Mat> hds;
  /*! \brief thread mining status */
  struct State {
    int current_idx;
    int current_hd_idx;
    double factor;
    int x, y;
    int win_size;
    int transform_type;
    int step;
    int reset;
    cv::Mat bg_img;
  };
  std::vector<State> states;
};

/*!
 * \brief DataSet Wrapper
 *  This class present the Pos data and Neg data, some functions can only be called by Pos data and some can
 *  only be called by Neg data. In order to support the faces which don't have the ground truth shape (in this way
 *  the algorithm can accepts more data), we use `shape_mask` to indicate where a face has a gt shape or not, Neg data
 *  are always be false.
 *
 * \note how to prepare the face which don't have gt shape
 *  In face.txt, every line indicate a face with image path at first, then the face bounding box and landmarks. If we set
 *  the landmark position less than zero, we assume this face don't have gt shape and set shape_mask = -1.
 *
 * \note
 *  You should regenerate jda_train_data.data if your previous data don't have shape_mask field.
 */
class DataSet {
public:
  DataSet();
  ~DataSet();

public:
  /*!
   * \brief Load Postive DataSet
   *  All positive samples are listed in this text file with each line represents a sample.
   *  We assume all positive samples are processed and generated before our program runs,
   *  this including resize the training samples, grayscale and data augmentation
   *
   * \param positive    a text file path
   */
  void LoadPositiveDataSet(const std::string& positive);
  /*!
   * \brief Load Negative DataSet
   *  We generate negative samples like positive samples before the program runs. Each line
   *  of the text file hold another text file which holds the real negative sample path in
   *  the filesystem, in this way, we can easily add more negative sample groups without
   *  touching other groups
   *
   * \param negative    negative text list
   */
  void LoadNegativeDataSet(const std::vector<std::string>& negative);
  /*!
   * \brief Wrapper for `LoadPositiveDataSet` and `LoadNegative DataSet`
   *  Since positive dataset and negative dataset may share some information between
   *  each other, we need to load them all together
   */
  static void LoadDataSet(DataSet& pos, DataSet& neg);
  /*!
   * \brief Calculate feature values from `feature_pool` with `idx`
   *
   * \param feature_pool    features
   * \param idx             index of dataset to calculate feature value
   * \return                every row presents a feature with every colum presents a data point
   *                        `feature_{i, j} = f_i(data_j)`
   */
  cv::Mat_<int> CalcFeatureValues(const std::vector<Feature>& feature_pool, \
                                  const std::vector<int>& idx) const;
  /*!
   * \brief Calcualte shape residual of landmark_id over positive dataset
   *  If a landmark id is given, we only generate the shape residual of that landmark
   * \param idx           index of positive dataset
   * \param landmark_id   landmark id to calculate shape residual
   * \return              every data point in each row
   */
  cv::Mat_<double> CalcShapeResidual(const std::vector<int>& idx) const;
  cv::Mat_<double> CalcShapeResidual(const std::vector<int>& idx, int landmark_id) const;
  /*!
   * \biref Calculate Mean Shape over gt_shapes
   * \return    mean_shape of gt_shapes in positive dataset
   */
  cv::Mat_<double> CalcMeanShape();
  /*!
   * \brief Random Shapes, a random perturbations on mean_shape
   * \param mean_shape    mean shape of positive samples
   * \param shape         random shape
   * \param shapes        this vector should already malloc memory for shapes
   */
  static void RandomShape(const cv::Mat_<double>& mean_shape, cv::Mat_<double>& shape);
  static void RandomShapes(const cv::Mat_<double>& mean_shape, std::vector<cv::Mat_<double> >& shapes);
  /*!
   * \brief Update weights
   *  `w_i = e^{-y_i*f_i}`, see more on paper in section 4.2
   */
  void UpdateWeights();
  static void UpdateWeights(DataSet& pos, DataSet& neg);
  /*!
   * \brief Update scores by cart
   *  `f_i = f_i + Cart(x, s)`, see more on paper in `Algorithm 3`
   */
  void UpdateScores(const Cart& cart);
  /*!
   * \brief Calculate threshold which seperate scores in two part
   *  `sum(scores < th) / N = rate`
   */
  double CalcThresholdByRate(double rate);
  double CalcThresholdByNumber(int remove);
  /*!
   * \brief Adjust DataSet by removing scores < th
   * \param th    threshold
   */
  void Remove(double th);
  /*!
   * \brief Get removed number if we perform remove operation
   * \param th    threshold
   */
  int PreRemove(double th);
  /*!
   * \brief Swap data point
   */
  void Swap(int i, int j);
  /*!
   * \brief More Negative Samples if needed (only neg dataset needs)
   * \param pos_size    positive dataset size, reference for generating
   * \param rate        N(negative) / N(positive)
   */
  void MoreNegSamples(int pos_size, double rate);
  /*!
   * \brief Quick Sort by scores descending
   */
  void QSort();
  void _QSort_(int left, int right);
  /*!
   * \brief Reset score to last_score
   */
  void ResetScores();
  /*!
   * \brief Calculate mean and std of scores
   * \param pos     positive dataset
   * \param neg     negative dataset
   * \param mean    mean of scores
   * \param std     std of scores
   */
  static void CalcMeanAndStd(const DataSet& pos, const DataSet& neg, double& mean, double& std);
  /*!
   * \brief Apply mean and std to scores
   * \brief mean    mean of scores
   * \brief std     std of scores
   */
  void ApplyMeanAndStd(const double mean, const double std);
  /*!
   * \brief Clear all
   */
  void Clear();
  /*!
   * \brief Snapshot all data into a binary file for Resume() maybe
   * \param   pos
   * \param   neg
   */
  static void Snapshot(const DataSet& pos, const DataSet& neg);
  /*!
   * \brief Resume data from a binary file generated by Snapshot
   * \note  it is useful to generate a binary file for training data which
   *        the load process may cost too much time if your data is very big
   *
   * \param data_file   data file path
   * \param pos         positive dataset
   * \param neg         negative dataset
   */
  static void Resume(const std::string& data_file, DataSet& pos, DataSet& neg);
  /*!
   * \brief Dump images to file system
   */
  void Dump(const std::string& dir) const;
  /*!
   * \brief query if the face has the shape
   * \param index   data index
   * \return        true for having the gt shape
   */
  inline bool HasGtShape(int index) const {
    if (is_pos && shape_mask[index] > 0) return true;
    else return false;
  }
  /*!
   * \brief calculate similarity transform parameter for every sample
   * \param mean_shape  mean shape of the face
   */
  void CalcSTParameters(const cv::Mat_<double>& mean_shape);

public:
  /*! \brief generator for more negative samples */
  NegGenerator neg_generator;
  /*! \brief face/none-face images */
  std::vector<cv::Mat> imgs;
  std::vector<cv::Mat> imgs_half;
  std::vector<cv::Mat> imgs_quarter;
  // all shapes follows (x_1, y_1, x_2, y_2, ... , x_n, y_n)
  /*! \brief ground-truth shapes for face */
  std::vector<cv::Mat_<double> > gt_shapes;
  /*! \brief shape mask, indicate whether this face has a gt shape, 1 for true and -1 for false */
  std::vector<int> shape_mask;
  /*! \brief current shapes */
  std::vector<cv::Mat_<double> > current_shapes;
  /*! \brief scores, see more about `f_i` on paper */
  std::vector<double> scores;
  std::vector<double> last_scores;
  /*! \brief weights, see more about `w_i` on paper */
  std::vector<double> weights;
  /*! \brief similarity transform parameters */
  std::vector<STParameter> stp_cm; // current_shape to mean_shape
  std::vector<STParameter> stp_mc; // mean_shape to current_shape
  /*! \brief is positive dataset */
  bool is_pos;
  /*! \brief mean shape of positive dataset */
  cv::Mat_<double> mean_shape;
  /*! \brief is sorted by scores */
  bool is_sorted;
  /*! \brief size of dataset */
  int size;
};

} // namespace jda

#endif // DATA_HPP_
