#pragma once

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

//Qt
#include <QString>
#include <QSharedPointer>

//system
#include <assert.h>

class ccPointCloud;

//! Generic feature descriptor
struct Feature
{
	//!Shared type
	typedef QSharedPointer<Feature> Shared;

	//! Set of features
	typedef std::vector<Shared> Set;

	//! Feature type
	enum class Type
	{
		PointFeature,			/*!< Point features (scalar field, etc.) */
		NeighborhoodFeature,	/*!< Neighborhood based features for a given scale */
		ContextBasedFeature,	/*!< Contextual based features */
		DualCloudFeature		/*!< Dual Cloud features: requires 2 point clouds */
	};

	//! Returns the type (must be reimplemented by child struct)
	virtual Type getType() = 0;

	//! Returns the formatted description
	virtual QString toString() const = 0;
		
	//! Sources of values for this feature
	enum Source
	{
		ScalarField, DimX, DimY, DimZ, Red, Green, Blue
	};

	//! Default constructor
	Feature(ccPointCloud* p_cloud, Source p_source, QString p_sourceName)
		: cloud(p_cloud)
		, source(p_source)
		, sourceName(p_sourceName)
	{
		assert(cloud);
	}

	//! Associated cloud
	ccPointCloud* cloud;
	//! Values source
	Source source;
	//! Feature source name (mandatory for scalar fields)
	QString sourceName;
};
