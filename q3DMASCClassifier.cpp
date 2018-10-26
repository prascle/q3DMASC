//##########################################################################
//#                                                                        #
//#                     CLOUDCOMPARE PLUGIN: q3DMASC                       #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 or later of the License.      #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#                 COPYRIGHT: Dimitri Lague / CNRS / UEB                  #
//#                                                                        #
//##########################################################################

#include "q3DMASCClassifier.h"

//Local
#include "ScalarFieldWrappers.h"

//qCC_db
#include <ccPointCloud.h>
#include <ccLog.h>

//qCC_io
#include <LASFields.h>

//Qt
#include <QCoreApplication>
#include <QProgressDialog>

using namespace masc;

Classifier::Classifier()
{
}

bool Classifier::isValid() const
{
	return (m_rtrees && m_rtrees->isTrained());
}

static QSharedPointer<IScalarFieldWrapper> GetSource(const Feature::Shared& f, ccPointCloud* cloud)
{
	QSharedPointer<IScalarFieldWrapper> source(nullptr);
	if (!f)
	{
		assert(false);
		ccLog::Warning(QObject::tr("Internal error: invalid feature (nullptr)"));
	}

	switch (f->source)
	{
	case Feature::ScalarField:
	{
		int sfIdx = cloud->getScalarFieldIndexByName(qPrintable(f->sourceName));
		if (sfIdx >= 0)
		{
			source.reset(new ScalarFieldWrapper(cloud->getScalarField(sfIdx)));
		}
		else
		{
			ccLog::Warning(QObject::tr("Internal error: unknwon scalar field '%1'").arg(f->sourceName));
		}
	}
	break;

	case Feature::DimX:
		source.reset(new DimScalarFieldWrapper(cloud, DimScalarFieldWrapper::DimX));
		break;
	case Feature::DimY:
		source.reset(new DimScalarFieldWrapper(cloud, DimScalarFieldWrapper::DimY));
		break;
	case Feature::DimZ:
		source.reset(new DimScalarFieldWrapper(cloud, DimScalarFieldWrapper::DimZ));
		break;

	case Feature::Red:
		source.reset(new ColorScalarFieldWrapper(cloud, ColorScalarFieldWrapper::Red));
		break;
	case Feature::Green:
		source.reset(new ColorScalarFieldWrapper(cloud, ColorScalarFieldWrapper::Green));
		break;
	case Feature::Blue:
		source.reset(new ColorScalarFieldWrapper(cloud, ColorScalarFieldWrapper::Blue));
		break;
	}

	return source;
}

bool Classifier::evaluate(const Feature::Set& features, CCLib::ReferenceCloud* testSubset, AccuracyMetrics& metrics, QString& errorMessage, QWidget* parentWidget/*=nullptr*/)
{
	metrics.sampleCount = metrics.goodGuess = 0;
	metrics.ratio = 0.0f;

	if (!m_rtrees || !m_rtrees->isTrained())
	{
		errorMessage = QObject::tr("Classifier hasn't been trained yet");
		return false;
	}

	if (features.empty())
	{
		errorMessage = QObject::tr("Training method called without any feature?!");
		return false;
	}
	if (!testSubset)
	{
		errorMessage = QObject::tr("No test subset provided");
		return false;
	}
	ccPointCloud* cloud = dynamic_cast<ccPointCloud*>(testSubset->getAssociatedCloud());
	if (!cloud)
	{
		errorMessage = QObject::tr("Invalid test subset (associated point cloud is not a ccPointCloud)");
		return false;
	}

	//look for the classification field
	int classifSFIdx = cloud->getScalarFieldIndexByName(LAS_FIELD_NAMES[LAS_CLASSIFICATION]); //LAS_FIELD_NAMES[LAS_CLASSIFICATION] = "Classification"
	if (!classifSFIdx)
	{
		errorMessage = QObject::tr("Missing 'Classification' field on input cloud");
		return false;
	}
	CCLib::ScalarField* classifSF = cloud->getScalarField(classifSFIdx);
	if (!classifSF || classifSF->size() < cloud->size())
	{
		assert(false);
		errorMessage = QObject::tr("Invalid 'Classification' field on input cloud");
		return false;
	}

	int testSampleCount = static_cast<int>(testSubset->size());
	int attributesPerSample = static_cast<int>(features.size());

	ccLog::Print(QObject::tr("[3DMASC] Testing data: %1 samples with %2 feature(s)").arg(testSampleCount).arg(attributesPerSample));

	//allocate the data matrix
	cv::Mat test_data;
	try
	{
		test_data.create(testSampleCount, attributesPerSample, CV_32FC1);
	}
	catch (const cv::Exception& cvex)
	{
		errorMessage = cvex.msg.c_str();
		return false;
	}

	//fill the data matrix
	for (int fIndex = 0; fIndex < attributesPerSample; ++fIndex)
	{
		const Feature::Shared &f = features[fIndex];
		if (!f)
		{
			assert(false);
			return false;
		}

		QSharedPointer<IScalarFieldWrapper> source = GetSource(f, cloud);
		if (!source || !source->isValid())
		{
			assert(false);
			errorMessage = QObject::tr("Internal error: invalid source '%1'").arg(f->sourceName);
			return false;
		}

		for (unsigned i = 0; i < testSubset->size(); ++i)
		{
			unsigned pointIndex = testSubset->getPointGlobalIndex(i);
			double value = source->pointValue(pointIndex);
			test_data.at<float>(i, fIndex) = static_cast<float>(value);
		}
	}

	//estimate the efficiency of the classiier
	{
		metrics.sampleCount = testSubset->size();
		metrics.goodGuess = 0;

		for (unsigned i = 0; i < testSubset->size(); ++i)
		{
			unsigned pointIndex = testSubset->getPointGlobalIndex(i);
			ScalarType pointClass = classifSF->getValue(pointIndex);
			int iClass = static_cast<int>(pointClass);
			//if (iClass < 0 || iClass > 255)
			//{
			//	errorMessage = QObject::tr("Classification values out of range (0-255)");
			//	return false;
			//}

			float predictedClass = m_rtrees->predict(test_data.row(i));
			if (static_cast<int>(predictedClass) == iClass)
			{
				++metrics.goodGuess;
			}
		}

		metrics.ratio = static_cast<float>(metrics.goodGuess) / metrics.sampleCount;
	}

	return true;
}

bool Classifier::train(const RandomTreesParams& params, const Feature::Set& features, QString& errorMessage, CCLib::ReferenceCloud* trainSubset/*=nullptr*/, QWidget* parentWidget/*=nullptr*/)
{
	if (features.empty())
	{
		errorMessage = QObject::tr("Training method called without any feature?!");
		return false;
	}
	if (!features.front() || !features.front()->cloud)
	{
		errorMessage = QObject::tr("Invalid feature (no associated point cloud)");
		return false;
	}
	ccPointCloud* cloud = features.front()->cloud;

	if (trainSubset && trainSubset->getAssociatedCloud() != cloud)
	{
		errorMessage = QObject::tr("Invalid train subset (associated point cloud is different)");
		return false;
	}

	//look for the classification field
	int classifSFIdx = cloud->getScalarFieldIndexByName(LAS_FIELD_NAMES[LAS_CLASSIFICATION]); //LAS_FIELD_NAMES[LAS_CLASSIFICATION] = "Classification"
	if (!classifSFIdx)
	{
		errorMessage = QObject::tr("Missing 'Classification' field on input cloud");
		return false;
	}
	CCLib::ScalarField* classifSF = cloud->getScalarField(classifSFIdx);
	if (!classifSF || classifSF->size() < cloud->size())
	{
		assert(false);
		errorMessage = QObject::tr("Invalid 'Classification' field on input cloud");
		return false;
	}

	int sampleCount = static_cast<int>(trainSubset ? trainSubset->size() : cloud->size());
	int attributesPerSample = static_cast<int>(features.size());

	ccLog::Print(QString("[3DMASC] Training data: %1 samples with %2 feature(s)").arg(sampleCount).arg(attributesPerSample));

	cv::Mat training_data, train_labels;
	try
	{
		training_data.create(sampleCount, attributesPerSample, CV_32FC1);
		train_labels.create(sampleCount, 1, CV_32FC1);
	}
	catch (const cv::Exception& cvex)
	{
		errorMessage = cvex.msg.c_str();
		return false;
	}

	//fill the classification labels vector
	{
		for (int i = 0; i < sampleCount; ++i)
		{
			int pointIndex = (trainSubset ? static_cast<int>(trainSubset->getPointGlobalIndex(i)) : i);
			ScalarType pointClass = classifSF->getValue(pointIndex);
			int iClass = static_cast<int>(pointClass);
			//if (iClass < 0 || iClass > 255)
			//{
			//	errorMessage = QObject::tr("Classification values out of range (0-255)");
			//	return false;
			//}

			train_labels.at<float>(i) = static_cast<unsigned char>(iClass);
		}
	}

	//fill the training data matrix
	for (int fIndex = 0; fIndex < attributesPerSample; ++fIndex)
	{
		const Feature::Shared &f = features[fIndex];
		if (f->cloud != cloud)
		{
			errorMessage = QObject::tr("Invalid feature (%1): associated cloud is different than the others").arg(f->toString());
			return false;
		}

		QSharedPointer<IScalarFieldWrapper> source = GetSource(f, cloud);
		if (!source || !source->isValid())
		{
			assert(false);
			errorMessage = QObject::tr("Internal error: invalid source '%1'").arg(f->sourceName);
			return false;
		}

		for (int i = 0; i < sampleCount; ++i)
		{
			int pointIndex = (trainSubset ? static_cast<int>(trainSubset->getPointGlobalIndex(i)) : i);
			double value = source->pointValue(pointIndex);
			training_data.at<float>(i, fIndex) = static_cast<float>(value);
		}
	}

	QProgressDialog pDlg(parentWidget);
	pDlg.setRange(0, 0); //infinite loop
	pDlg.setLabelText("Training classifier");
	pDlg.show();
	QCoreApplication::processEvents();

	m_rtrees = cv::ml::RTrees::create();
	m_rtrees->setMaxDepth(params.maxDepth);
	m_rtrees->setMinSampleCount(params.minSampleCount);
	m_rtrees->setCalculateVarImportance(params.calcVarImportance);
	m_rtrees->setActiveVarCount(params.activeVarCount);
	cv::TermCriteria terminationCriteria(cv::TermCriteria::MAX_ITER, params.maxTreeCount, std::numeric_limits<double>::epsilon());
	m_rtrees->setTermCriteria(terminationCriteria);
	
	//rtrees->setRegressionAccuracy(0);
	//rtrees->setUseSurrogates(false);
	//rtrees->setMaxCategories(params.maxCategories); //not important?
	//rtrees->setPriors(cv::Mat());
	try
	{
		m_rtrees->train(training_data, cv::ml::ROW_SAMPLE, train_labels);
	}
	catch (const cv::Exception& cvex)
	{
		m_rtrees.release();
		errorMessage = cvex.msg.c_str();
		return false;
	}
	catch (const std::exception& stdex)
	{
		errorMessage = stdex.what();
		return false;
	}
	catch (...)
	{
		errorMessage = QObject::tr("Unknown error");
		return false;
	}

	pDlg.hide();
	QCoreApplication::processEvents();

	if (!m_rtrees->isTrained())
	{
		errorMessage = QObject::tr("Training failed for an unknown reason...");
		m_rtrees.release();
		return false;
	}

	return true;
}

bool Classifier::toFile(QString filename, QWidget* parentWidget/*=nullptr*/) const
{
	if (!m_rtrees)
	{
		ccLog::Warning(QObject::tr("Classifier hasn't been trained, can't save it"));
		return false;
	}
	
	//save the classifier
	QProgressDialog pDlg(parentWidget);
	pDlg.setRange(0, 0); //infinite loop
	pDlg.setLabelText(QObject::tr("Saving classifier"));
	pDlg.show();
	QCoreApplication::processEvents();

	m_rtrees->save(filename.toStdString());
	
	pDlg.close();
	QCoreApplication::processEvents();

	ccLog::Print("Classifier file saved to: " + filename);
	return true;
}

bool Classifier::fromFile(QString filename, QWidget* parentWidget/*=nullptr*/)
{
	//load the classifier
	QProgressDialog pDlg(parentWidget);
	pDlg.setRange(0, 0); //infinite loop
	pDlg.setLabelText(QObject::tr("Loading classifier"));
	pDlg.show();
	QCoreApplication::processEvents();

	m_rtrees = cv::ml::RTrees::load(filename.toStdString());

	pDlg.close();
	QCoreApplication::processEvents();

	if (!m_rtrees->isTrained())
	{
		ccLog::Warning(QObject::tr("Loaded classifier doesn't seem to be trained"));
	}

	return true;
}