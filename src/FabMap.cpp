/*
 * FabMap.cpp
 *
 *  Created on: 16/03/2012
 *      Author: will
 */

#include "../include/openfabmap.hpp"

using std::vector;
using std::list;
using std::map;
using std::set;
using std::valarray;
using cv::Mat;

double logsumexp(double a, double b) {
	return a > b ? log(1 + exp(b - a)) + a : log(1 + exp(a - b)) + b;
}

namespace of2 {

FabMap::FabMap(const Mat& _clTree, double _PzGe,
		double _PzGNe, int _flags, int _numSamples) :
	clTree(_clTree), PzGe(_PzGe), PzGNe(_PzGNe), flags(
			_flags), numSamples(_numSamples) {
	CV_Assert(flags & MEAN_FIELD || flags & SAMPLED);
	CV_Assert(flags & NAIVE_BAYES || flags & CHOW_LIU);
}

FabMap::~FabMap() {
}

void FabMap::addTraining(const Mat& imgDescriptors) {

	CV_Assert(!imgDescriptors.empty());
	CV_Assert(imgDescriptors.cols == clTree.cols);
	for (int i = 0; i < imgDescriptors.rows; i++) {
		trainingImgDescriptors.push_back(imgDescriptors.rowRange(i,i));
	}

}

void FabMap::match(const Mat& queryImgDescriptors, vector<IMatch>& matches) {
	CV_Assert(!queryImgDescriptors.empty());
	CV_Assert(!testImgDescriptors.empty());
	CV_Assert(queryImgDescriptors.cols == clTree.cols);

	matches.clear();

	for (int i = 0; i < queryImgDescriptors.rows; i++) {

		vector<IMatch> queryMatches;
		Mat queryImgDescriptor = queryImgDescriptors.rowRange(i,i);
		queryMatches.push_back(IMatch(i,-1,getNewPlaceLikelihood(queryImgDescriptor),0));
		getLikelihoods(queryImgDescriptor,testImgDescriptors,queryMatches);
		normaliseDistribution(queryMatches);

		testImgDescriptors.push_back(queryImgDescriptors.rowRange(i,i));
		matches.insert(matches.end(), queryMatches.begin(), queryMatches.end());
	}

}

void FabMap::match(const Mat& queryImgDescriptors, const Mat& testImgDescriptors,
		vector<IMatch>& matches) {
	CV_Assert(!queryImgDescriptors.empty());
	CV_Assert(queryImgDescriptors.cols == clTree.cols);
	CV_Assert(!testImgDescriptors.empty());
	CV_Assert(testImgDescriptors.cols == clTree.cols);
	CV_Assert(!(flags & MOTION_MODEL));

	vector<Mat> testImgDescriptorsVec;
	for (int i = 0; i < testImgDescriptors.rows; i++) {
		testImgDescriptorsVec.push_back(testImgDescriptors.rowRange(i,i));
	}

	matches.clear();

	vector<Mat> queryImgDescriptorsVec;
	for (int i = 0; i < queryImgDescriptors.rows; i++) {
		Mat queryImgDescriptor = queryImgDescriptors.rowRange(i,i);

	}
	//match(queryImgDescriptorsVec,testImgDescriptorsVec,matches);
}

void FabMap::getLikelihoods(const Mat& queryImgDescriptor,
		const vector<Mat>& testImgDescriptors, vector<IMatch>& matches) {

}

double FabMap::getNewPlaceLikelihood(const Mat& queryImgDescriptor) {
	if (flags & MEAN_FIELD) {

		bool Sq, Sp;
		double logP = 0;

		for (int word = 0; word < clTree.cols; word++) {
			Sq = queryImgDescriptor.at<float>(0,word) > 0;
			Sp = queryImgDescriptor.at<float>(0,parent(word)) > 0;

			double alpha, beta, p;
			alpha = P(word, Sq) * Pzge(!Sq, false) * PqGp(word, !Sq, Sp);
			beta = P(word, !Sq) * Pzge(Sq, false) * PqGp(word, Sq, Sp);
			p = P(word, false) * beta / (alpha + beta);

			alpha = P(word, Sq) * Pzge(!Sq, true) * PqGp(word, !Sq, Sp);
			beta = P(word, !Sq) * Pzge(Sq, true) * PqGp(word, Sq, Sp);
			p += P(word, true) * beta / (alpha + beta);

			logP += log(p);
		}

		return logP;

	}
	if (flags & SAMPLED) {
		CV_Assert(!trainingImgDescriptors.empty());
		CV_Assert(numSamples > 0);

		vector<Mat> sampledImgDescriptors;

		for (int i = 0; i < numSamples; i++) {
			int index = rand() % trainingImgDescriptors.size();
			sampledImgDescriptors.push_back(trainingImgDescriptors[index]);
		}

		vector<IMatch> matches;
		getLikelihoods(queryImgDescriptor,sampledImgDescriptors,matches);

		double averageLikelihood = 0;
		for (int i = 0; i < numSamples; i++) {
			averageLikelihood += matches[i].likelihood;
		}

		return averageLikelihood / (double)numSamples;

	}
	return 0;
}

void FabMap::normaliseDistribution(vector<IMatch>& matches) {
	CV_Assert(!matches.empty());

	double logsum = -DBL_MAX;

	if (flags & MOTION_MODEL) {
		if (matches[0].imgIdx == -1) {
			matches[0].match = matches[0].likelihood + log(Pnew);
		}

		if (priorMatches.size() > 2) {
			matches[1].match += log((priorMatches[1].match +
							2 * mBias * priorMatches[2].match) / 3);
			for (size_t i = 2; i < priorMatches.size()-1; i++) {
				matches[i].match += log((2 * (1-mBias) * priorMatches[i-1].match
								+ priorMatches[i].match + 2 * mBias * priorMatches[i+1].match)/3);
			}
			matches[priorMatches.size()-1].match +=
			log((2 * (1-mBias) * priorMatches[priorMatches.size()-2].match
							+ priorMatches[priorMatches.size()-1].match)/3);
		}

		for (size_t i = 0; i < matches.size(); i++) {
			logsum = logsumexp(logsum, matches[i].match);
		}
		for (size_t i = 0; i < matches.size(); i++) {
			matches[i].match = exp(matches[i].match - logsum);
		}

	} else {
		for (size_t i = 0; i < matches.size(); i++) {
			logsum = logsumexp(logsum, matches[i].likelihood);
		}
		for (size_t i = 0; i < matches.size(); i++) {
			matches[i].match = exp(matches[i].likelihood - logsum);
		}
	}

	for (size_t i = 0; i < matches.size(); i++) {
		matches[i].match = sFactor*matches[i].match +
		(1 - sFactor)/matches.size();
	}

}

int FabMap::parent(int word) {
	return (int)clTree.at<double>(0,word);
}

double FabMap::P(int word, bool q) {
	return (q) ? clTree.at<double>(1,word) : 1 - clTree.at<double>(1,word);
}

double FabMap::PqGp(int word, bool q, bool p) {
	if (p) {
		return (q) ? clTree.at<double>(2,word) : 1 - clTree.at<double>(2,word);
	} else {
		return (q) ? clTree.at<double>(3,word) : 1 - clTree.at<double>(3,word);
	}
}

double FabMap::Pzge(bool Zi, bool ei) {
	if (ei) {
		return (Zi) ? PzGe : 1 - PzGe;
	} else {
		return (Zi) ? PzGNe : 1 - PzGNe;
	}
}

double FabMap::PeGl(int word, bool zi, bool ei) {
	double alpha, beta;
	alpha = Pzge(zi, true) * P(word, true);
	beta = Pzge(zi, false) * P(word, false);

	if (ei) {
		return alpha / (alpha + beta);
	} else {
		return 1 - alpha / (alpha + beta);
	}
}

double FabMap::Pqgp(bool Zq, bool Zpq, bool Lq, int q) {
	double p;
	double alpha, beta;

	double Pegl, Pnegl;
	alpha = Pzge(Lq, true) * P(q, true);
	beta = Pzge(Lq, false) * P(q, false);
	Pegl = alpha / (alpha + beta);
	Pnegl = 1 - Pegl;

	alpha = P(q, Zq) * Pzge(!Zq, false) * PqGp(q, !Zq, Zpq);
	beta = P(q, !Zq) * Pzge( Zq, false) * PqGp(q, Zq, Zpq);
	p = Pnegl * beta / (alpha + beta);

	alpha = P(q, Zq) * Pzge(!Zq, true) * PqGp(q, !Zq, Zpq);
	beta = P(q, !Zq) * Pzge( Zq, true) * PqGp(q, Zq, Zpq);
	p += Pegl * beta / (alpha + beta);

	return p;
}


FabMap1::FabMap1(const Mat& _clTree, double _PzGe, double _PzGNe, int _flags, int _numSamples) :
FabMap(_clTree, _PzGe, _PzGNe, _flags, _numSamples) {
}

FabMap1::~FabMap1() {
}

void FabMap1::getLikelihoods(const Mat& queryImgDescriptor,
		const vector<Mat>& testImgDescriptors, vector<IMatch>& matches) {

	matches.clear();

	for (size_t i = 0; i < testImgDescriptors.size(); i++) {
		bool Zq, Sq, Sp;
		double logP = 0;
		for (int word = 0; word < clTree.cols; word++) {
			Zq = testImgDescriptors[i].at<float>(0,word) > 0;
			Sq = queryImgDescriptor.at<float>(0,word) > 0;
			Sp = queryImgDescriptor.at<float>(0,parent(word)) > 0;

			double alpha, beta, p;
			alpha = P(word, Sq) * Pzge(!Sq, false) * PqGp(word, !Sq, Sp);
			beta = P(word, !Sq) * Pzge(Sq, false) * PqGp(word, Sq, Sp);
			p = PeGl(word, Zq, false) * beta / (alpha + beta);

			alpha = P(word, Sq) * Pzge(!Sq, true) * PqGp(word, !Sq, Sp);
			beta = P(word, !Sq) * Pzge(Sq, true) * PqGp(word, Sq, Sp);
			p += PeGl(word, Zq, true) * beta / (alpha + beta);

			logP += log(p);
		}
		matches.push_back(IMatch(0,i,logP,0));
	}
}

FabMapLUT::FabMapLUT(const Mat& _clTree, double _PzGe, double _PzGNe, int _precision, int _flags, int _numSamples) :
FabMap(_clTree, _PzGe, _PzGNe, _flags, _numSamples), precision(_precision) {

	int nWords = clTree.cols;
	double precFactor = (double)pow(10.0,precision);

	table = new int[nWords][8];
	double alpha, beta, p;

	for (int word = 0; word < nWords; word++) {
		for (unsigned char i = 0; i < 8; i++) {

			bool Zq = (bool) ((i >> 2) & 0x01);
			bool Sq = (bool) ((i >> 1) & 0x01);
			bool Sp = (bool) (i & 1);

			double Pegl, Pnegl;
			alpha = Pzge(Zq, true) * P(word, true);
			beta = Pzge(Zq, false) * P(word, false);
			Pegl = alpha / (alpha + beta);
			Pnegl = 1 - Pegl;

			alpha = P(word, Sq) * Pzge(!Sq, false) * PqGp(word, !Sq, Sp);
			beta = P(word, !Sq) * Pzge(Sq, false) * PqGp(word, Sq, Sp);
			p = Pnegl * beta / (alpha + beta);

			alpha = P(word, Sq) * Pzge(!Sq, true) * PqGp(word, !Sq, Sp);
			beta = P(word, !Sq) * Pzge(Sq, true) * PqGp(word, Sq, Sp);
			p += Pegl * beta / (alpha + beta);

			table[word][i] = (int)(log(p)*precFactor);
		}
	}
}

FabMapLUT::~FabMapLUT() {
	delete[] table;
}

void FabMapLUT::getLikelihoods(const Mat& queryImgDescriptor,
		const vector<Mat>& testImgDescriptors, vector<IMatch>& matches) {

	matches.clear();

	double precFactor = (double)pow(10.0,-precision);

	for (size_t i = 0; i < testImgDescriptors.size(); i++) {
		long int logP = 0;
		for (int word = 0; word < clTree.cols; word++) {
			logP += table[word][(queryImgDescriptor.at<float>(0,parent(word)) > 0) +
			((queryImgDescriptor.at<float>(0, word) > 0) << 1) +
			((testImgDescriptors[i].at<float>(0,word) > 0) << 2)];
		}
		matches.push_back(IMatch(0,i,precFactor*(double)logP,0));
	}
}

FabMapFBO::FabMapFBO(const Mat& _clTree, double _PzGe,
		double _PzGNe, double _PS_D, double _rejectionThreshold, int _bisectionStart, int _bisectionIts, int _flags, int _numSamples) :
FabMap(_clTree, _PzGe, _PzGNe, _flags, _numSamples), PS_D(_PS_D), rejectionThreshold(_rejectionThreshold),
bisectionStart(_bisectionStart), bisectionIts(_bisectionIts) {
}

FabMapFBO::~FabMapFBO() {
}

void FabMapFBO::getLikelihoods(const Mat& queryImgDescriptor,
		const vector<Mat>& testImgDescriptors, vector<IMatch>& matches) {

	set<WordStats> wordData;
	setWordStatistics(queryImgDescriptor, wordData);

	vector<int> matchIndices;

	for (size_t i = 0; i < testImgDescriptors.size(); i++) {
		matches.push_back(IMatch(0,i,0,0));
		matchIndices.push_back(i);
	}

	for (set<WordStats>::reverse_iterator wordIter = wordData.rbegin();
			wordIter != wordData.rend(); wordIter++) {
		bool Sq = queryImgDescriptor.at<float>(0,wordIter->word)>0;
		bool Sp = queryImgDescriptor.at<float>(0,parent(wordIter->word))>0;

		double currBest = -DBL_MAX;

		for (size_t i = 0; i < matchIndices.size(); i++) {
			bool Zq = testImgDescriptors[matchIndices[i]].at<float>(0,wordIter->word)>0;
			matches[matchIndices[i]].likelihood += log(Pqgp(Sq,Sp,Zq,wordIter->word));
			currBest = std::max(matches[matchIndices[i]].likelihood,currBest);
		}

		if (matchIndices.size() == 1)
			continue;

		double delta = std::max(limitbisection(wordIter->V, wordIter->M), -log(rejectionThreshold));

		vector<int>::iterator matchIter = matchIndices.begin(), removeIter;
		while (matchIter != matchIndices.end()) {
			if (currBest - matches[*matchIter].likelihood > delta) {
				matches[*matchIter].likelihood = -log(rejectionThreshold);
				removeIter = matchIter;
				matchIter++;
				matchIndices.erase(removeIter);
			} else {
				matchIter++;
			}
		}
	}

}

void FabMapFBO::setWordStatistics(const Mat& queryImgDescriptor,
		set<WordStats>& wordData) {
		for (int i = 0; i < clTree.cols; i++) {
			wordData.insert(WordStats(i,PqGp(i,
					queryImgDescriptor.at<float>(0,i)>0,
					queryImgDescriptor.at<float>(0,parent(i))>0)));
		}

		double d = 0, V = 0, M = 0;

		for (set<WordStats>::iterator wordIter = wordData.begin();
				wordIter != wordData.end(); wordIter++) {
			d = Pqgp(queryImgDescriptor.at<float>(0,wordIter->word)>0,
						queryImgDescriptor.at<float>(0,parent(wordIter->word))>0,
						true, wordIter->word);
				- Pqgp(queryImgDescriptor.at<float>(0,wordIter->word)>0,
						queryImgDescriptor.at<float>(0,parent(wordIter->word))>0,
						false, wordIter->word);
			V += pow(d, 2.0) * 2 * (P(wordIter->word, true) - pow(P(wordIter->word, true), 2.0));
			wordIter->V = V;
			M = std::max(M, fabs(d));
			wordIter->M = M;
		}
}

double FabMapFBO::limitbisection(double v, double m) {
	double midpoint, left_val, mid_val;
	double left = 0, right = bisectionStart;

	left_val = bennettInequality(v, m, left) - PS_D;

	for(int i = 0; i < bisectionIts; i++) {

		midpoint = (left + right)*0.5;
		mid_val = bennettInequality(v, m, midpoint)- PS_D;

		if(left_val * mid_val > 0) {
			left = midpoint;
			left_val = mid_val;
		} else {
			right = midpoint;
		}
	}

	return (right + left) * 0.5;
}

double FabMapFBO::bennettInequality(double v, double m, double delta) {
	double DMonV = delta * m / v;
	double f_delta = log(DMonV + sqrt(pow(DMonV, 2.0) + 1));
	return exp((v / pow(m, 2.0))*(cosh(f_delta) - 1 - DMonV * f_delta));
}

bool FabMapFBO::compInfo(const WordStats& first, const WordStats& second) {
	return first.info < second.info;
}

FabMap2::FabMap2(const Mat& _clTree, double _PzGe, double _PzGNe, int _flags, int _numSamples) :
FabMap(_clTree, _PzGe, _PzGNe, _flags, _numSamples) {
	CV_Assert(flags & SAMPLED);

	d1.resize(clTree.cols);
	d2.resize(clTree.cols);
	d3.resize(clTree.cols);
	d4.resize(clTree.cols);

	for (int word = 0; word < clTree.cols; word++) {
		d1[word] = log(Pqgp(false, false, true, word) /
				Pqgp(false, false, false, word));
		d2[word] = log(Pqgp(false, true, true, word) /
				Pqgp(false, true, false, word)) - d1[word];
		d3[word] = log(Pqgp(true, false, true, word) /
				Pqgp(true, false, false, word))- d1[word];
		d4[word] = log(Pqgp(true, true, true, word) /
				Pqgp(true, true, false, word))- d1[word];
		children[parent(word)].push_back(word);
	}

}

FabMap2::~FabMap2() {
}

void FabMap2::addTraining(const Mat& imgDescriptors) {
	FabMap::addTraining(imgDescriptors);

	trainingDefaults.resize(trainingImgDescriptors.size());
	for (size_t i = 0; i < trainingImgDescriptors.size(); i++) {
		for (int j = 0; j < clTree.cols; j++) {
			if (trainingImgDescriptors[i].at<float>(0,j)) {
				trainingDefaults[i] += d1[j];
				trainingInvertedMap[j].push_back(i);
			}
		}
	}
}

void FabMap2::getLikelihoods(const Mat& queryImgDescriptor,
		const vector<Mat>& _testImgDescriptors, vector<IMatch>& matches) {
	if (_testImgDescriptors[0].data == testImgDescriptors[0].data) {
		getIndexLikelihoods(queryImgDescriptor, testDefaults, testInvertedMap, matches);
		addToIndex(queryImgDescriptor, testDefaults, testInvertedMap);
	} else {
		getIndexLikelihoods(queryImgDescriptor, trainingDefaults, trainingInvertedMap, matches);
	}
}

void FabMap2::addToIndex(const Mat& queryImgDescriptor,
		vector<double>& defaults,
		map<int, vector<int> >& invertedMap) {
	defaults.push_back(0);
	for (int word = 0; word < clTree.cols; word++) {
		if (queryImgDescriptor.at<float>(0,word) > 0) {
			defaults.back() += d1[word];
			invertedMap[word].push_back((int)defaults.size());
		}
	}
}

void FabMap2::getIndexLikelihoods(const Mat& queryImgDescriptor,
		vector<double>& defaults,
		map<int, vector<int> >& invertedMap,
		vector<IMatch>& matches) {

	vector<int>::iterator LwithI, child;
	vector<double> TnMLogLs = defaults;

	for (int word = 0; word < clTree.cols; word++) {
		if (queryImgDescriptor.at<float>(0,word)) {
			for (LwithI = invertedMap[word].begin(); LwithI != invertedMap[word].end(); LwithI++) {
				if (queryImgDescriptor.at<float>(0,parent(word)) > 0) {
					TnMLogLs[*LwithI] += d4[word];
				} else {
					TnMLogLs[*LwithI] += d3[word];
				}
			}
			for (child = children[word].begin(); child != children[word].end(); child++) {
				if (queryImgDescriptor.at<float>(0,*child) == 0) {
					for (LwithI = invertedMap[*child].begin(); LwithI != invertedMap[*child].end(); LwithI++) {
						TnMLogLs[*LwithI] += d1[word];
					}
				}
			}
		}
	}
	for (size_t i = 0; i < TnMLogLs.size(); i++) {
		matches.push_back(IMatch(0, i, TnMLogLs[i],0));
	}
}

}
