/* perceptron_generator.h                                          -*- C++ -*-
   Jeremy Barnes, 15 March 2006
   Copyright (c) 2006 Jeremy Barnes.  All rights reserved.
   $Source$

   Generator for a perceptron.
*/

#ifndef __boosting__perceptron_generator_h__
#define __boosting__perceptron_generator_h__


#include "boosting/early_stopping_generator.h"
#include "perceptron.h"


namespace ML {


/*****************************************************************************/
/* PERCEPTRON_GENERATOR                                                      */
/*****************************************************************************/

/** Class to generate a classifier.  The meta-algorithms (bagging, boosting,
    etc) can use this algorithm to generate weak-learners.
*/

class Perceptron_Generator : public Early_Stopping_Generator {
public:
    Perceptron_Generator();

    virtual ~Perceptron_Generator();

    /** Configure the generator with its parameters. */
    virtual void
    configure(const Configuration & config);
    
    /** Return to the default configuration. */
    virtual void defaults();

    /** Return possible configuration options. */
    virtual Config_Options options() const;

    /** Initialize the generator, given the feature space to be used for
        generation. */
    virtual void init(boost::shared_ptr<const Feature_Space> fs,
                      Feature predicted);

    using Early_Stopping_Generator::generate;

    /** Generate a classifier from one training set. */
    virtual boost::shared_ptr<Classifier_Impl>
    generate(Thread_Context & context,
             const Training_Data & training_data,
             const Training_Data & validation_data,
             const distribution<float> & training_weights,
             const distribution<float> & validation_weights,
             const std::vector<Feature> & features, int) const;

    unsigned max_iter;
    unsigned min_iter;
    float learning_rate;
    Transfer_Function_Type activation, output_activation;
    bool do_decorrelate;
    bool do_normalize;
    float batch_size;
    bool use_cuda;
    int training_mode;
    int training_algo;
    int min_examples_per_job;
    int max_examples_per_job;
    bool use_textures;
    float target_value;
    bool order_of_sensitivity;

    std::string arch_str;

    /* Once init has been called, we clone our potential models from this
       one. */
    Perceptron model;



    boost::multi_array<float, 2>
    init(const Training_Data & data,
         const std::vector<Feature> & possible_features,
         const std::vector<int> & architecture,
         Perceptron & perceptron,
         Thread_Context & context) const;


    float train_weighted(const Training_Data & data,
                         const boost::multi_array<float, 2> & weights,
                         int parent) const;
    
    /** Decorrelate the training data, returning a matrix of decorrelated
        data.  Messages will be printed out as features are removed.  The
        Features variable will be set up here and a first layer of the
        network will be added (that simply performs the decorrelation).
    */
    boost::multi_array<float, 2>
    decorrelate(const Training_Data & data,
                const std::vector<Feature> & possible_features,
                Perceptron & result) const;
    
    /** Decorrelate another training data object using the decorrelation
        already trained. */
    boost::multi_array<float, 2>
    decorrelate(const Training_Data & data) const;
        
    /** Trains a single iteration.  Returns the accuracy over the training
        corpus of the previous iteration.
    */
    std::pair<double, double>
    train_iteration(Thread_Context & context,
                    const boost::multi_array<float, 2> & decorrelated,
                    const std::vector<Label> & labels,
                    const distribution<float> & example_weights,
                    Perceptron & result) const;
};


} // namespace ML


#endif /* __boosting__perceptron_generator_h__ */