/*
Copyright 2016 Salles V. G. Magalhaes, W. R. Franklin, Marcus Andrade

This file is part of PinMesh.

PinMesh is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

PinMesh is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with PinMesh.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>
#include <vector>
#include <array>
#include <map>
#include <cstdlib>
#include <unordered_set>
#include <unordered_map>
#include <time.h>
#include "rationals.h"
#include <omp.h>



using namespace std;


//===============================================================
// General templates (from WRF source codes)
//===============================================================

template <class T>
T max(const T a, const T b, const T c, const T d) {
  return max(max(a,b), max(c,d));
}

template <class T>
T min(const T a, const T b, const T c, const T d) {
  return min(min(a,b), min(c,d));
}

template <class T, class U>
void accum_max(T &maxsofar, const U x) {
  if (x>maxsofar) maxsofar = x;
}

template <class T>
void accum_min(T &minsofar, const T x) {
  if (x<minsofar) minsofar = x;
}

template <class T>
T square(const T x) { return x*x; }

template <class T>    // Squared length from dx and dy
T sqlen(const T dx, const T dy) { return square(dx)+square(dy); }


//From Boost, for using pairs in unordered_sets:
template <class T>
inline void hash_combine(std::size_t & seed, const T & v)
{
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace std
{
  template<typename S, typename T> struct hash<pair<S, T>>
  {
    inline size_t operator()(const pair<S, T> & v) const
    {
      size_t seed = 0;
      ::hash_combine(seed, v.first);
      ::hash_combine(seed, v.second);
      return seed;
    }
  };
}


// Utils for measuring time...

double convertTimeMsecs(const timespec &t){
  return (t.tv_sec*1000 + t.tv_nsec/1000000.0);
}

//source: http://www.guyrutenberg.com/2007/09/22/profiling-code-using-clock_gettime/
timespec diff(timespec start, timespec end)
{
        timespec temp;
        if ((end.tv_nsec-start.tv_nsec)<0) {
                temp.tv_sec = end.tv_sec-start.tv_sec-1;
                temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
        } else {
                temp.tv_sec = end.tv_sec-start.tv_sec;
                temp.tv_nsec = end.tv_nsec-start.tv_nsec;
        }
        return temp;
}

double timeReadData,timeCreateGrid,timeDetectIntersections,timeRetesselate,timeClassifyTriangles;

//===============================================================
// Constants...
//===============================================================



//===============================================================
#include "3d_objects.cpp"

#include "nested3DGrid.cpp"

#include "tritri_isectline.c"


//Each vector represents the vertices of a layer
//The first layear is mesh 0, the second is mesh1 and the third contains vertices generated from the intersection of the two meshes...
vector<Point> vertices[3]; 

//Each vector represents a set of objects in the same layer
//The objects are represented by a set of triangles (defining their boundaries)
vector<Triangle> triangles[2]; 
//we don't need bounding-boxes for the output triangles...
vector<TriangleNoBB> trianglesFromRetesselation[2]; //triangles formed by retesselating triangles from each mesh...

Point bBox[2][2] ;//bounding box of each mesh (each boundingbox has two vertices)
Point boundingBoxTwoMeshesTogetter[2]; //bounding box considering both meshes togetter

Nested3DGridWrapper uniformGrid;

timespec t0BeginProgram, t0AfterDatasetRead;

//if vertexId negative, the point is shared...
Point * getPointFromVertexId(int vertexId, int meshId) {
 return (vertexId>=0)?&vertices[meshId][vertexId]:&vertices[2][-vertexId -1];
}

//=======================================================================================================================


#include "PinMesh.cpp"





const int PLANE_X0 =0;
const int PLANE_Y0 =1;
const int PLANE_Z0 =2;
// TODO: perform ONLY necessary computation..
//TODO: example we do not need z to check if the z component of the normal is 0...
//Given a triangle, returns a plane (X=0,Y=0 or Z=0) such that the triangle is not perpendicular to that plane..
//A triangle is perpendicular to a plane iff its normal has the corresponding coordinate equal to 0
//a = p2-p1
//b = p3-p1
//normal: (a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x)
//tempVars should have at least 8 elements...
int getPlaneTriangleIsNotPerpendicular(const Point &p1, const Point &p2,const Point &p3, VertCoord tempVars[]) {
  //a = (tempVars[0],tempVars[1],tempVars[2])
  tempVars[0]=p2[0];
  tempVars[0]-=p1[0];

  tempVars[1]=p2[1];
  tempVars[1]-=p1[1];

  
  //b = (tempVars[3],tempVars[4],tempVars[5])
  tempVars[3]=p3[0]; 
  tempVars[3]-=p1[0];

  tempVars[4]=p3[1];
  tempVars[4]-=p1[1];

  

  //Let's check if the z-component of the normal is 0
  //if it is --> the triangle is perpendicular to z=0...
  tempVars[6] = tempVars[0]; //a.x
  tempVars[6] *= tempVars[4]; //b.y
  tempVars[7] = tempVars[1]; //a.y
  tempVars[7] *=tempVars[3]; //b.x
  tempVars[6] -= tempVars[7]; //a.x*b.y - a.y*b.x
  if(sgn(tempVars[6])!=0) return PLANE_Z0; //the triangle is NOT perpendicular to z=0...


  tempVars[2]=p2[2];
  tempVars[2]-=p1[2];

  tempVars[5]=p3[2];
  tempVars[5]-=p1[2];

  tempVars[6] = tempVars[2]; //a.z
  tempVars[6] *= tempVars[3]; //b.x
  tempVars[7] = tempVars[0]; //a.x
  tempVars[7] *=tempVars[5]; //b.z
  tempVars[6] -= tempVars[7]; //a.z*b.x - a.x*b.z
  if(sgn(tempVars[6])!=0) return PLANE_Y0; //the triangle is NOT perpendicular to y=0...
  
  return PLANE_X0; //we do not need to check... (unless the triangle is just a point...)

}




void extractPairsTrianglesInGridCell(const Nested3DGrid *grid,int i,int j, int k, int gridSize,vector<pair<Triangle *,Triangle *> > &pairsTrianglesToProcess) {
	int numTrianglesMesh0 = grid->numTrianglesInGridCell(0, gridSize,i,j,k);//cell.triangles[0].size();
  int numTrianglesMesh1 = grid->numTrianglesInGridCell(1, gridSize,i,j,k);



  Triangle **ptrTriMesh0 = grid->getPointerStartListTriangles(0,gridSize,i,j,k);
  Triangle **ptrTriMesh1Temp = grid->getPointerStartListTriangles(1,gridSize,i,j,k);

  for(int tA = 0;tA < numTrianglesMesh0; tA++) {
  	Triangle **ptrTriMesh1 = ptrTriMesh1Temp;
    for(int tB=0;tB<numTrianglesMesh1;tB++) {
      pairsTrianglesToProcess.push_back(pair<Triangle *,Triangle *>(*ptrTriMesh0, *ptrTriMesh1));
      ptrTriMesh1++;
    }
    ptrTriMesh0++;
  }
}

//the vector will be filled with pointers to the triangles in the same uniform grid cells
//pairs will appear maximum once in the vector
//the first element in the pair is a triangle from mesh 0 and the second one is from mesh 1
void getPairsTrianglesInSameUnifGridCells(const Nested3DGridWrapper *uniformGrid,vector<pair<Triangle *,Triangle *> > &pairsTrianglesToProcess) {
	//timespec t0,t1;
	//clock_gettime(CLOCK_REALTIME, &t0);

	pairsTrianglesToProcess.reserve(min(uniformGrid->trianglesInGrid[0]->size(),uniformGrid->trianglesInGrid[1]->size()));

  int gridSizeLevel1 =  uniformGrid->gridSizeLevel1;
	int gridSizeLevel2 =  uniformGrid->gridSizeLevel2;
  for(int i=0;i<gridSizeLevel1;i++) 
    for(int j=0;j<gridSizeLevel1;j++) 
      for(int k=0;k<gridSizeLevel1;k++) {
        if (uniformGrid->grid.hasSecondLevel(i,j,k) ) {
        	Nested3DGrid *secondLevelGrid = uniformGrid->grid.getChildGrid(i,j,k); 
			    for(int iLevel2=0;iLevel2<gridSizeLevel2;iLevel2++) 
			    	for(int jLevel2=0;jLevel2<gridSizeLevel2;jLevel2++) 
			      	for(int kLevel2=0;kLevel2<gridSizeLevel2;kLevel2++) {
			      		  extractPairsTrianglesInGridCell(secondLevelGrid,iLevel2,jLevel2,kLevel2,gridSizeLevel2,pairsTrianglesToProcess);   
			      	}    
        } else {
        	extractPairsTrianglesInGridCell(&(uniformGrid->grid),i,j,k,gridSizeLevel1,pairsTrianglesToProcess);         
        }
      }

//  clock_gettime(CLOCK_REALTIME, &t1);
  //cerr << "T before sort: " << convertTimeMsecs(diff(t0,t1))/1000 << "\n"; 

  sort(pairsTrianglesToProcess.begin(),pairsTrianglesToProcess.end());
  vector<pair<Triangle *,Triangle *> >::iterator it = std::unique (pairsTrianglesToProcess.begin(), pairsTrianglesToProcess.end());
  pairsTrianglesToProcess.resize( std::distance(pairsTrianglesToProcess.begin(),it) ); // 10 20 30 20 10 


 // clock_gettime(CLOCK_REALTIME, &t1);
  //cerr << "T after sort: " << convertTimeMsecs(diff(t0,t1))/1000 << "\n";    
}


array<double,3> toDoublePoint(const Point &p) {
	array<double,3> pnew;
  for(int i=0;i<3;i++) {
    pnew[i] = p[i].get_d();
  }
  return pnew;
}

/*
void saveEdges(const string &path) {
  ofstream fout(path.c_str());

  int numVert = 3*edges.size();
  int numEdges = 3*edges.size();
  int numFaces = edges.size();
  fout << numVert << " " << numEdges << " " << numFaces << "\n";
  for(pair< array<VertCoord,3>,array<VertCoord,3> > e:edges) {
    fout << e.first[0].get_d() << " " << e.first[1].get_d() << " " << e.first[2].get_d() << "\n";
    fout << e.second[0].get_d() << " " << e.second[1].get_d() << " " << e.second[2].get_d() << "\n";
    fout << e.second[0].get_d() << " " << e.second[1].get_d() << " " << e.second[2].get_d() << "\n";
  }
  fout << endl;

  int start = 1;
  for(int i=0;i<numFaces;i++) {
    fout << start << " " << start+1 << "\n";
    fout << start+1 << " " << start+2 << "\n";
    fout << start+2 << " " << start << "\n";
    start+= 3;
  }
  fout << endl;
  start = 1;
  for(int i=0;i<numFaces;i++) {
    fout << start << " " << start+1 << " " << start+2 <<  "\n";
    start+= 3;
  }
}
*/
void storeEdgesAsGts(const string &path,const vector<pair<array<double,3>,array<double,3>> > &edgesToStore) {
	/*map<array<double,3>, int> vertexToId;

	vector<array<double,3> > vertices;
	for(auto &e:edgesToStore) {
		if(vertexToId.count(e.first)==0) {
			int id = vertexToId.size();
			vertexToId[e.first] = id;
			vertices.push_back(e.first);
		}
		if(vertexToId.count(e.second)==0) {
			int id = vertexToId.size();
			vertexToId[e.second] = id;
			vertices.push_back(e.second);
		}
	}*/

	ofstream fout(path.c_str());
	int numVert = 3*edgesToStore.size();
  int numEdges = 3*edgesToStore.size();
  int numFaces = edgesToStore.size();

	fout << numVert << " " << numEdges << " " << numFaces << "\n";
	for(pair<array<double,3>,array<double,3>> e:edgesToStore) {
    fout << e.first[0] << " " << e.first[1] << " " << e.first[2] << "\n";
    fout << e.second[0] << " " << e.second[1] << " " << e.second[2] << "\n";
    fout << e.second[0] << " " << e.second[1] << " " << e.second[2] << "\n";
  }
  int start = 1;
  for(int i=0;i<numFaces;i++) {
    fout << start << " " << start+1 << "\n";
    fout << start+1 << " " << start+2 << "\n";
    fout << start+2 << " " << start << "\n";
    start+= 3;
  }  
  start = 1;
  for(int i=0;i<numFaces;i++) {
    fout << start << " " << start+1 << " " << start+2 <<  "\n";
    start+= 3;
  }
}



void storeOneTriangleAsGts(const string &path,array<double,3> a,array<double,3> b, array<double,3> c,bool reverseOrientation) {
	ofstream fout(path.c_str());
	int numVert = 3;
  int numEdges = 3;
  int numFaces = 1;

	fout << numVert << " " << numEdges << " " << numFaces << "\n";
  fout << a[0] << " " << a[1] << " " << a[2] << "\n";
  fout << b[0] << " " << b[1] << " " << b[2] << "\n";
  fout << c[0] << " " << c[1] << " " << c[2] << "\n";
  
  if(!reverseOrientation) {
  	fout << 1 << " " << 2 << "\n";
  	fout << 2 << " " << 3 << "\n";
  	fout << 3 << " " << 1 << "\n";
  	fout << "1 2 3"<< "\n";   	  	  	
  } else {
  	fout << 1 << " " << 3 << "\n";
  	fout << 3 << " " << 2 << "\n";
  	fout << 2 << " " << 1 << "\n";
  	fout << "1 2 3"<< "\n";   	
  }
}



void storeTriangleIntersections(const vector<pair<Triangle *,Triangle *> >  &pairsIntersectingTriangles) {
	cerr << "Storing triangles that intersect (and intersection edges...)" << endl;
	map<Triangle *, vector<Triangle *> > trianglesFromOtherMeshIntersectingThisTriangle[2];
	
	for(auto &p:pairsIntersectingTriangles) {
		trianglesFromOtherMeshIntersectingThisTriangle[0][p.first].push_back(p.second);
		trianglesFromOtherMeshIntersectingThisTriangle[1][p.second].push_back(p.first);
	}

	VertCoord p0InterLine[3],p1InterLine[3];
  VertCoord tempRationals[100];
  int coplanar;

  
	for(int meshId=0;meshId<2;meshId++) {
		vector<pair<array<double,3>,array<double,3>> > edgesToStore;
		int ctTriangles = 0;
		for(auto &p:trianglesFromOtherMeshIntersectingThisTriangle[meshId]) {
			Triangle *t0 = p.first;
			vector<Triangle *> &trianglesIntersectingT0 = p.second;

			
			edgesToStore.push_back(make_pair(toDoublePoint(vertices[meshId][t0->p[0]]),toDoublePoint(vertices[meshId][t0->p[1]])));
			edgesToStore.push_back(make_pair(toDoublePoint(vertices[meshId][t0->p[1]]),toDoublePoint(vertices[meshId][t0->p[2]])));
			edgesToStore.push_back(make_pair(toDoublePoint(vertices[meshId][t0->p[2]]),toDoublePoint(vertices[meshId][t0->p[0]])));
			//stringstream outputTrianglePath,outputTriangleCutPath;
			//outputTrianglePath << "out/triangle_" << meshId << "_" << ctTriangles++  << ".gts";
			//outputTriangleCutPath << "out/cut_triangle_" << meshId << "_" << ctTriangles << ".gts";

			//storeEdgesAsGts(outputTrianglePath.str(),edgesToStore);

			Triangle &a = *t0;
			for(auto t1:trianglesIntersectingT0) {				
      	Triangle &b = *t1;
      
      	int ans = tri_tri_intersect_with_isectline(vertices[meshId][a.p[0]].data(),vertices[meshId][a.p[1]].data(),vertices[meshId][a.p[2]].data()     ,     
                                                  vertices[1-meshId][b.p[0]].data(),vertices[1-meshId][b.p[1]].data(),vertices[1-meshId][b.p[2]].data(),
                                                  &coplanar, p0InterLine ,p1InterLine,tempRationals);

      	assert(ans);
      	if(!coplanar) {
      		array<double,3> p0,p1;
      		for(int i=0;i<3;i++) {
      			p0[i] = p0InterLine[i].get_d();
      			p1[i] = p1InterLine[i].get_d();
      		}
      		edgesToStore.push_back(make_pair(p0,p1));
      	} else {
      		cerr << "Coplanar found!" << endl << endl << endl;
      	}
			}
			
		}
		if(meshId==0)
			storeEdgesAsGts("out/triangles0Intersect.gts",edgesToStore);
		else 
			storeEdgesAsGts("out/triangles1Intersect.gts",edgesToStore);
	}
	
}




//--------------------------------------------------------------
//Source: http://www.geeksforgeeks.org/check-if-two-given-line-segments-intersect/

// To find orientation of ordered triplet (p, q, r).
// The function returns following values
// 0 --> p, q and r are colinear
// 1 --> Clockwise
// 2 --> Counterclockwise
//tempVars should have at least 3 elements...
//
int orientation(const Point &p, const Point &q, const Point &r, int whatPlaneProjectTo,  VertCoord tempVars[])
{
    /*VertCoord val = (q[1] - p[1]) * (r[0] - q[0]) -
              (q[0] - p[0]) * (r[1] - q[1]);
 
    if (val == 0) return 0;  // colinear
 
    return (val > 0)? 1: 2; // clock or counterclock wise*/

    //we will project the points to the plane whatPlaneProjectTriangleTo
    //what coordinates should we check during computations?
    //if the points are projected to z=0 --> we have to use x and y
    //if the points are projected to y=0 --> we have to use x and z
    //...............................x=0 --> we have to use y and z
    int coord1=0,coord2=1;
    if(whatPlaneProjectTo==PLANE_Y0) {
      coord1=0;
      coord2=2;
    } else if(whatPlaneProjectTo==PLANE_X0) {
      coord1=1;
      coord2=2;
    }

    tempVars[0] = q[coord2];
    tempVars[0] -= p[coord2];
    tempVars[1] = r[coord1];
    tempVars[1] -= q[coord1];
    tempVars[0]*=tempVars[1];//tempVars[0] = (q[1] - p[1]) * (r[0] - q[0])

    tempVars[2] = q[coord1];
    tempVars[2] -=p[coord1];
    tempVars[1] = r[coord2];
    tempVars[1] -= q[coord2];
    tempVars[1] *= tempVars[2]; //tempVars[1] =  (q[0] - p[0]) * (r[1] - q[1])

   /* tempVars[0] -= tempVars[1]; 
    int sign = sgn(tempVars[0]);
    if(sign==0) return 0;
    return sign>0?1:2; */
    if(tempVars[0]==tempVars[1]) return 0;
    if(tempVars[0]>tempVars[1]) return 1;
    return 2;
}

//TODO : reduce amount of tests...
//TODO: maybe use address of vertices to reduce amount of tests...
//TODO: avoid memory copy... (remove max, min, etc);
// Given three colinear points p, q, r, the function checks if
// point q lies on line segment 'pr'
//All segments are co-planar (obvious) and are projected to plane whatPlaneProjectTriangleTo
bool onSegment(const Point & p, const Point & q, const Point & r, int whatPlaneProjectTo)
{
		//if (&q==&r || &q==&p) return false; //the endpoints coincide...
    //we will project the points to the plane whatPlaneProjectTriangleTo
    //what coordinates should we check during computations?
    //if the points are projected to z=0 --> we have to use x and y
    //if the points are projected to y=0 --> we have to use x and z
    //...............................x=0 --> we have to use y and z
    int coord1=0,coord2=1;
    if(whatPlaneProjectTo==PLANE_Y0) {
      coord1=0;
      coord2=2;
    } else if(whatPlaneProjectTo==PLANE_X0) {
      coord1=1;
      coord2=2;
    }

    if (q[coord1] < max(p[coord1], r[coord1]) && q[coord1] > min(p[coord1], r[coord1]) &&
        q[coord2] <= max(p[coord2], r[coord2]) && q[coord2] >= min(p[coord2], r[coord2]))
       return true;

    if (q[0] <= max(p[coord1], r[coord1]) && q[coord1] >= min(p[coord1], r[coord1]) &&
        q[coord2] < max(p[coord2], r[coord2]) && q[coord2] > min(p[coord2], r[coord2])) 
    	return true;
 
    return false;
}
 
// TODO: special cases!!!
//do p1-q1 intersect p2-q2 ?
//the two segments are co-planar
//during computation, we project them to whatPlaneProjectTriangleTo plane... 
bool doIntersect(const Point &p1, const Point &q1, const Point &p2, const Point &q2, int whatPlaneProjectTriangleTo, VertCoord tempVars[])
{
    // Find the four orientations needed for general and
    // special cases
    int o1 = orientation(p1, q1, p2,whatPlaneProjectTriangleTo,tempVars);

    // Special Cases
    // p1, q1 and p2 are colinear and p2 lies on segment p1q1
    if (o1 == 0 && onSegment(p1, p2, q1,whatPlaneProjectTriangleTo)) return true;


    int o2 = orientation(p1, q1, q2,whatPlaneProjectTriangleTo,tempVars);

    // p1, q1 and p2 are colinear and q2 lies on segment p1q1
    if (o2 == 0 && onSegment(p1, q2, q1,whatPlaneProjectTriangleTo)) return true;

    //the two segments are collinear, but they do not intersect 
    if(o1==0 && o2==0) return false;


    int o3 = orientation(p2, q2, p1,whatPlaneProjectTriangleTo,tempVars);

    // p2, q2 and p1 are colinear and p1 lies on segment p2q2
    if (o3 == 0 && onSegment(p2, p1, q2,whatPlaneProjectTriangleTo)) return true;

    int o4 = orientation(p2, q2, q1,whatPlaneProjectTriangleTo,tempVars); 
    
 
     // p2, q2 and q1 are colinear and q1 lies on segment p2q2
    if (o4 == 0 && onSegment(p2, q1, q2,whatPlaneProjectTriangleTo)) return true;

    // General case
    if (o1!=0 && o2!=0 && o3!=0  && o4!=0)
      if (o1 != o2 && o3 != o4) {
    	 return true;
      }

 
    return false; // Doesn't fall in any of the above cases
}



//returns true iff the candidate edge (represented using the ids of the vertices in meshIdToProcess -- negative vertices are in the "common layer")
//intersects an edge from the set edgesToTest (these edges are in the same layer).
//we do not consider intersections in endpoints..
//TODO: consider special cases: vertical triangle, parallel edges, etc...
long long ctEdgeIntersect  = 0; //how many of the intersection tests performed are true?
long long ctEdgeDoNotIntersect = 0;
bool intersects(const pair<int,int> &candidateEdge,const set<pair<int,int> > &edgesToTest,int meshIdToProcess, int whatPlaneProjectTriangleTo, VertCoord tempVars[]) {
	const Point *v1CandidateEdge = (candidateEdge.first>=0)?&vertices[meshIdToProcess][candidateEdge.first]:&vertices[2][-candidateEdge.first -1];
  const Point *v2CandidateEdge = (candidateEdge.second>=0)?&vertices[meshIdToProcess][candidateEdge.second]:&vertices[2][-candidateEdge.second -1];
  
  //cerr << "Testing intersection: " << candidateEdge.first << " " << candidateEdge.second << endl;

  for(const pair<int,int> &edgeToTest:edgesToTest) {


  	const Point *v1EdgeToTest = (edgeToTest.first>=0)?&vertices[meshIdToProcess][edgeToTest.first]:&vertices[2][-edgeToTest.first -1];
  	const Point *v2EdgeToTest = (edgeToTest.second>=0)?&vertices[meshIdToProcess][edgeToTest.second]:&vertices[2][-edgeToTest.second -1];
  	
  	//check if edges (v1EdgeToTest,v2EdgeToTest) intersects (v1CandidateEdge,v2CandidateEdge) (it is ok if they intersect at common vertices..)
  	//if (v1EdgeToTest==v1CandidateEdge || v1EdgeToTest==v2CandidateEdge || v2EdgeToTest==v1CandidateEdge || v2EdgeToTest==v2CandidateEdge )
  	//	continue; //they intersect at common vertices...

  	bool inter = doIntersect(*v1CandidateEdge,*v2CandidateEdge,*v1EdgeToTest,*v2EdgeToTest,whatPlaneProjectTriangleTo,tempVars);
  	
    if(inter) ctEdgeIntersect++;
    else ctEdgeDoNotIntersect++;

  	if(inter) 
  		return true;

  }
  return false;
}



//TODO: avoid memory allocation!!!
bool isInTriangleProj(const Point &p, const Point &p0,const Point &p1,const Point &p2, int whatPlaneProjectTriangleTo) {
  if ( p==p0 || p==p1 || p==p2) return false; //is the point directly above a vertex of the triangle?

  //what coordinates should we check during computations?
  //if the points are projected to z=0 --> we have to use x and y
  //if the points are projected to y=0 --> we have to use x and z
  //...............................x=0 --> we have to use y and z
  int coord1=0,coord2=1;
  if(whatPlaneProjectTriangleTo==PLANE_Y0) {
    coord1=0;
    coord2=2;
  } else if(whatPlaneProjectTriangleTo==PLANE_X0) {
    coord1=1;
    coord2=2;
  }

  
  VertCoord denominator = ((p1[coord2] - p2[coord2])*(p0[coord1] - p2[coord1]) + (p2[coord1] - p1[coord1])*(p0[coord2] - p2[coord2]));
  if (denominator==0) { //TODO: check this.... degenerate triangles or vertical triangles (a segment never intersects a vertical triangle...)
    return false;
  }
  VertCoord a = ((p1[coord2] - p2[coord2])*(p[coord1] - p2[coord1]) + (p2[coord1] - p1[coord1])*(p[coord2] - p2[coord2])) / denominator;
  if ( a<=0 || a >=1) return false;
  
  VertCoord b = ((p2[coord2] - p0[coord2])*(p[coord1] - p2[coord1]) + (p0[coord1] - p2[coord1])*(p[coord2] - p2[coord2])) / denominator;

  if (b<=0 || b>=1) return false;
  VertCoord c = 1 - a - b;
  
  return 0 < c && c < 1; 
}


//returns true iff one of the vertices (represented by their id) is completely inside the triangle a,b,c (represented by ids of vertices)
//the positives ids represent vertices from the meshId mesh
//all vertices are coplanar...thus, we just check if a vertex is in the projection of a,b,c to z=0 (TODO: consider vertical triangle..try to project to x=0 and also y=0 depending on the triangle...)
//we check if the point is completely inside the triangle (i.e., not in the border or outside..)
bool isThereAVertexInsideTriangle(const vector<int> &vertices,const int p0, const int p1, const int p2, const int meshId, int whatPlaneProjectTriangleTo) {
	for(const int v:vertices) {
		if(isInTriangleProj(*getPointFromVertexId(v,meshId),*getPointFromVertexId(p0,meshId),*getPointFromVertexId(p1,meshId),*getPointFromVertexId(p2,meshId), whatPlaneProjectTriangleTo)) {
			return true;
		}
	}
	return false;
}

//seedEdge is an edge containint originalTriangleRetesselated.p[0] and that is collinear with (originalTriangleRetesselated.p[0],originalTriangleRetesselated.p[1])
//it will be use to determine the orientation of the retesselated triangles...
//meshIdToProcess is the id of the mesh of the triangle being split..
void createNewTrianglesFromRetesselationAndOrient(const set<pair<int,int> > &edgesUsedInThisTriangle,const Triangle &originalTriangleRetesselated,vector<TriangleNoBB> &newTrianglesFromRetesselation,  pair<int,int> seedEdge,const int meshIdToProcess, const int  whatPlaneProjectTriangleTo) {
	map<int,int>  verticesStartingFrom0; //TODO: optimize this part...
	vector<int> vertexFrom0ToVertexId;

	vector<pair<int,int> > edgesInTriangleCountingFrom0;
	for(const pair<int,int> &p:edgesUsedInThisTriangle) {
		if(verticesStartingFrom0.count(p.first)==0) {
			verticesStartingFrom0[p.first] = vertexFrom0ToVertexId.size();
			vertexFrom0ToVertexId.push_back(p.first);
		}
		if(verticesStartingFrom0.count(p.second)==0) {
			verticesStartingFrom0[p.second] = vertexFrom0ToVertexId.size();
			vertexFrom0ToVertexId.push_back(p.second);
		}

		edgesInTriangleCountingFrom0.push_back(make_pair(verticesStartingFrom0[p.first],verticesStartingFrom0[p.second]));
	}
	int numVertices = vertexFrom0ToVertexId.size();

	int idOfTV0 = verticesStartingFrom0[originalTriangleRetesselated.p[0]]; //what is the label (in our graph) of the two vertices (v[0] and v[1]) from the original triangle 

	bool seedEdgeSameOrientationOriginalTriangle = seedEdge.first==originalTriangleRetesselated.p[0];

	seedEdge.first = verticesStartingFrom0[seedEdge.first];
	seedEdge.second = verticesStartingFrom0[seedEdge.second];

	bool adjMatrix[numVertices][numVertices];
	for(int i=0;i<numVertices;i++)
		for(int j=0;j<numVertices;j++)
			adjMatrix[i][j] = false;

	//cerr << "Number of edges to triangulate: " << edgesInTriangleCountingFrom0.size() << endl;
	//cerr << "Number of vertices: " << numVertices << endl;
	for(const pair<int,int> &p:edgesInTriangleCountingFrom0) {
		//cerr << p.first << "  " << p.second << endl;
		adjMatrix[p.first][p.second] = adjMatrix[p.second][p.first] = true;
		//cerr << p.first << " " << p.second << endl;
		assert(p.first!=p.second);
	}

	set< pair<int,pair<int,int>> > trianglesProcessed;
	queue<pair<pair<int,int>,pair<int,int> > > edgesToProcess;

	if(seedEdgeSameOrientationOriginalTriangle)
		edgesToProcess.push( make_pair(seedEdge,make_pair(originalTriangleRetesselated.above, originalTriangleRetesselated.below)));
	else 
		edgesToProcess.push( make_pair(seedEdge,make_pair(originalTriangleRetesselated.below,originalTriangleRetesselated.above)));

	set<pair<int,int> > processedEdges;
	while(!edgesToProcess.empty()) {
		int i = edgesToProcess.front().first.first;
		int j = edgesToProcess.front().first.second;
		int above = edgesToProcess.front().second.first;
		int below = edgesToProcess.front().second.second;
		edgesToProcess.pop();


		pair<int,int> edge(min(i,j),max(i,j)); //let's use this to uniquely identify each edge so that we do not process the same edge twice...
		if(processedEdges.count(edge)!=0) {
			continue; //the triangles containting that edge were already processed...
		}
		processedEdges.insert(edge);

		
		//we have an edge (i,j) with the same orientation as originalTriangleRetesselated
		for(int k=0;k<numVertices;k++) {
			if(adjMatrix[i][k] && adjMatrix[j][k]) {

				pair<int,pair<int,int>> triangle;
				if (i<j && i<k) {
					triangle.first = i;

					if(j<k) {
						triangle.second.first = j;
						triangle.second.second = k;
					} else {
						triangle.second.first = k;
						triangle.second.second = j;
					}
				} else 	if (j<i && j<k) {
					triangle.first = j;

					if(i<k) {
						triangle.second.first = i;
						triangle.second.second = k;
					} else {
						triangle.second.first = k;
						triangle.second.second = i;
					}
				} else {
					triangle.first = k;

					if(i<j) {
						triangle.second.first = i;
						triangle.second.second = j;
					} else {
						triangle.second.first = j;
						triangle.second.second = i;
					}
				}
			
				if(trianglesProcessed.count(triangle)!=0) {
					continue;
				}

				//if a vertex from the retesselation is inside this candidate triangle, we will not create this triangle..
				//test all "numVertices" vertices in vertexFrom0ToVertexId
				if(isThereAVertexInsideTriangle(vertexFrom0ToVertexId,vertexFrom0ToVertexId[i],vertexFrom0ToVertexId[j],vertexFrom0ToVertexId[k],meshIdToProcess,whatPlaneProjectTriangleTo)) {
					//cerr << "Ignoring triangle: " << vertexFrom0ToVertexId[i] << " " << vertexFrom0ToVertexId[j] << " " << vertexFrom0ToVertexId[k] << endl;
					continue;
				}

				//cerr << "Adding: " << vertexFrom0ToVertexId[i] << " " << vertexFrom0ToVertexId[j] << " " << vertexFrom0ToVertexId[k] << endl;
					//we have a triangle i,j,k...
				assert(vertexFrom0ToVertexId[i] != vertexFrom0ToVertexId[j]);
				assert(vertexFrom0ToVertexId[i] != vertexFrom0ToVertexId[k]);
				assert(vertexFrom0ToVertexId[j] != vertexFrom0ToVertexId[k]);
				newTrianglesFromRetesselation.push_back(TriangleNoBB(vertexFrom0ToVertexId[i],vertexFrom0ToVertexId[j],vertexFrom0ToVertexId[k],above,below)); //TODO: fix orientation...
				trianglesProcessed.insert(triangle);
				edgesToProcess.push(make_pair(make_pair(j,k),make_pair(below,above)));
				edgesToProcess.push(make_pair(make_pair(k,i),make_pair(below,above)));
			}
		}
	}

	/*
	for(const pair<int,int> &p:edgesInTriangleCountingFrom0) {
			int i=p.first;
			int j=p.second;
			for(int k=0;k<numVertices;k++) {
				if(adjMatrix[i][k] && adjMatrix[j][k]) {
					//we have a triangle i,j,k...
					assert(vertexFrom0ToVertexId[i] != vertexFrom0ToVertexId[j]);
					assert(vertexFrom0ToVertexId[i] != vertexFrom0ToVertexId[k]);
					assert(vertexFrom0ToVertexId[j] != vertexFrom0ToVertexId[k]);
					newTrianglesFromRetesselation.push_back(TriangleNoBB(vertexFrom0ToVertexId[i],vertexFrom0ToVertexId[j],vertexFrom0ToVertexId[k],-1,0)); //TODO: fix orientation...
				}
			}
		}
	*/
}


int numVerticesInEdges = 0;
//stores some counters (statistics)
struct StatisticsAboutRetesseation {
  StatisticsAboutRetesseation() {
    ctTrianglesRetesselate = 0;
    ctEdgesActuallyInsertedInRetesselation =0;
    ctEdgesInsertedBecauseOfConstraint=0;
    ctVerticesInsertedTrianglesToRetesselate=0;
    ctEdgesTestedToInsertInRetesselation=0;
    simple = difficult = 0;

  }
  void printStatsSoFar() {
    cerr << numVerticesInEdges << endl;
    cerr << "Num triangles to retesselate                                 : "  << ctTrianglesRetesselate << "\n";
    cerr << "Total number of edges we tried to insert during retesselation: "  << ctEdgesTestedToInsertInRetesselation << "\n";  
    cerr << "Total edges tried insert and actually inserted               : " << ctEdgesActuallyInsertedInRetesselation  <<"\n";
    cerr << "Total edges inserted because are constraint edges            : " << ctEdgesInsertedBecauseOfConstraint <<"\n";
    cerr << "Total number of edges in retesselated triangles (add prev. 2): " << ctEdgesInsertedBecauseOfConstraint+ctEdgesActuallyInsertedInRetesselation <<"\n";
    cerr << "Total number of vertices in retesselated triangles           : "  << ctVerticesInsertedTrianglesToRetesselate << "\n";
    cerr << "Simple case triangles                                        : " << simple << endl;
    cerr << "Difficult case triangles                                     : " << difficult << endl;
  }
  long long simple, difficult;
  long long ctTrianglesRetesselate;
  long long ctEdgesActuallyInsertedInRetesselation;
  long long ctEdgesInsertedBecauseOfConstraint;
  long long ctVerticesInsertedTrianglesToRetesselate;
  long long ctEdgesTestedToInsertInRetesselation;
};


//given a triangle t, this function will split t at the intersection edges (intersection with other triangles)
//and retesselate t, creating more triangles.
void retesselateTriangle(const vector<pair<int,int> > &edgesUsingVertexId, const vector<int> &edgesFromIntersection,const Triangle &t, const int meshWhereTriangleIs, vector<TriangleNoBB> &newTrianglesGeneratedFromRetesselation,VertCoord tempVars[], StatisticsAboutRetesseation &statistics) {
  //A triangle (except if it is degenerate to a single point) cannot be perpendicular to all 3 planes (x=0,y=0,z=0)
  //Thus, we chose one of these planes to project the triangle during computations...
  const int whatPlaneProjectTriangleTo = getPlaneTriangleIsNotPerpendicular(vertices[meshWhereTriangleIs][t.p[0]], vertices[meshWhereTriangleIs][t.p[1]],vertices[meshWhereTriangleIs][t.p[2]], tempVars);

  //we need to "retesselate" t and orient all the new triangles properly
  //this set will store the edges we used in the retesselated triangle
  //we need to choose what edges to create and, then, use these new edges to reconstruct the triangulation..
  //This set will have the edges that will form the retriangulation of ts..
  set<pair<int,int> > edgesUsedInThisTriangle; //TODO: maybe use unordered_set (see overhead difference...)


  int ct =0;
  //The edges from intersection will, necessarelly, be in the triangulation...
  for(int edgeId:edgesFromIntersection) {
    //cerr << edgesUsingVertexId[edgeId].first << " " << edgesUsingVertexId[edgeId].second << endl;
    edgesUsedInThisTriangle.insert(edgesUsingVertexId[edgeId]);   
    statistics.ctEdgesInsertedBecauseOfConstraint++;   
  }


  //Now, we need to add more edges to fill the parts of ts that still do not form triangle
  vector<int> verticesToTesselate;
  //what vertices will we have in the new triangulation of ts? (the original vertices + the vertices of the edges from intersections)
  for(const auto &p:edgesUsedInThisTriangle) {
    verticesToTesselate.push_back(p.first);
    verticesToTesselate.push_back(p.second);
  }
  verticesToTesselate.push_back(t.p[0]);
  verticesToTesselate.push_back(t.p[1]);
  verticesToTesselate.push_back(t.p[2]);
  sort(verticesToTesselate.begin(),verticesToTesselate.end());
  auto newEnd = unique(verticesToTesselate.begin(),verticesToTesselate.end());
  verticesToTesselate.resize(newEnd-verticesToTesselate.begin());
  int numVTriangle = verticesToTesselate.size();

  statistics.ctVerticesInsertedTrianglesToRetesselate += numVTriangle;


  

  const Point &triangleVertex0 = vertices[meshWhereTriangleIs][t.p[0]];
  const Point &triangleVertex1 = vertices[meshWhereTriangleIs][t.p[1]];
  const Point &triangleVertex2 = vertices[meshWhereTriangleIs][t.p[2]];
  //cerr << triangleVertex0[0].get_d( ) << " " << triangleVertex0[1].get_d() << " " << triangleVertex0[2].get_d() << endl;;
  //cerr << triangleVertex1[0].get_d( ) << " " << triangleVertex1[1].get_d() << " " << triangleVertex1[2].get_d() << endl;;
  //cerr << triangleVertex2[0].get_d( ) << " " << triangleVertex2[1].get_d() << " " << triangleVertex2[2].get_d() << endl;;
  //cerr << "Project to: " << whatPlaneProjectTriangleTo << endl;


  //verticesIncidentEachEdgeOriginalTriangle[0] is for vertex t.p[0]-t.p[1]
  //verticesIncidentEachEdgeOriginalTriangle[1] is for vertex t.p[1]-t.p[2]
  //verticesIncidentEachEdgeOriginalTriangle[2] is for vertex t.p[2]-t.p[0]
  list<int> verticesIncidentEachEdgeOriginalTriangle[3];
  for(int i=0;i<numVTriangle;i++) {
    int v = verticesToTesselate[i];
    if(v==t.p[0] || v==t.p[1] || v==t.p[2]) continue;
    //for each vertex v that is not one of the original vertices of the triangle, let's see if v is in one of the edges of t...

    const Point &vertex = *getPointFromVertexId(v, meshWhereTriangleIs);
    //cerr << "V: " << vertex[0].get_d( ) << " " << vertex[1].get_d() << " " << vertex[2].get_d() << endl;;
    int o1 = orientation(triangleVertex0, triangleVertex1,  vertex,whatPlaneProjectTriangleTo,tempVars);
    
    
    //cerr << o1 << o2 << o3 << endl;
    if(o1==0) {
      verticesIncidentEachEdgeOriginalTriangle[0].push_back(i);
      continue;
    }
    int o2 = orientation(triangleVertex1, triangleVertex2,  vertex,whatPlaneProjectTriangleTo,tempVars);
    if(o2==0) {
      //cerr << "12" << endl;
      verticesIncidentEachEdgeOriginalTriangle[1].push_back(i);
      continue;
    }
    int o3 = orientation(triangleVertex2, triangleVertex0,  vertex,whatPlaneProjectTriangleTo,tempVars);
    if(o3==0) {
     // cerr << 20 << endl;
      verticesIncidentEachEdgeOriginalTriangle[2].push_back(i);
      continue;
    }
  }
  //cerr << "In edges: " << numVerticesInEdges << "\n";
 // cerr << "Vertices: " << numVTriangle << "\n";
  //cerr << "Constraint edges: " << edgesFromIntersection.size() << "\n\n";

  pair<int,int> seedEdge(-1,-1); //we need a seed output edge that will be oriented in the same way t is oriented..
                           //we will use this seed to reorient all the triangles resulting from the retesselation of t

  bool allSimple = true;
  //no vertex intersect the edge t.p[0]-t.p[1] --> it will be in the triangulation!!!
  int verticesIncidentEdge;
  verticesIncidentEdge = verticesIncidentEachEdgeOriginalTriangle[0].size();
  if(verticesIncidentEdge==0) {
    int v1 = t.p[0];
    int v2 = t.p[1];
            
    if(v2<v1) swap(v1,v2); //the first vertex is always smaller than the second in the edges... (to simplify tests)
    pair<int,int> candidateEdge(v1,v2);

    if (v1 == t.p[0] && v2 == t.p[1]) {
      seedEdge.first = t.p[0];
      seedEdge.second = t.p[1];
    } else if  (v1 == t.p[1] && v2 == t.p[0]) {
      seedEdge.first = t.p[1];
      seedEdge.second = t.p[0];
    }

    statistics.ctEdgesActuallyInsertedInRetesselation++;
    edgesUsedInThisTriangle.insert(candidateEdge);
  } else {
    if(verticesIncidentEdge==1) {
      int v1 = t.p[0];
      int v2 = t.p[1]; 
      int v =  verticesToTesselate[*(verticesIncidentEachEdgeOriginalTriangle[0].begin())];          
      
      pair<int,int> candidateEdge;    

      if(v1<v) {candidateEdge.first = v1; candidateEdge.second = v;}
      else {candidateEdge.first = v; candidateEdge.second = v1;}
      statistics.ctEdgesActuallyInsertedInRetesselation++;
      edgesUsedInThisTriangle.insert(candidateEdge);

      if(v2<v) {candidateEdge.first = v2; candidateEdge.second = v;}
      else {candidateEdge.first = v; candidateEdge.second = v2;}
      statistics.ctEdgesActuallyInsertedInRetesselation++;
      edgesUsedInThisTriangle.insert(candidateEdge);
    } else {
      allSimple= false;
    }
  }

  verticesIncidentEdge = verticesIncidentEachEdgeOriginalTriangle[1].size();
  if(verticesIncidentEdge==0) {
    int v1 = t.p[1];
    int v2 = t.p[2];
            
    if(v2<v1) swap(v1,v2); //the first vertex is always smaller than the second in the edges... (to simplify tests)
    pair<int,int> candidateEdge(v1,v2);    

    statistics.ctEdgesActuallyInsertedInRetesselation++;
    edgesUsedInThisTriangle.insert(candidateEdge);
  } else {
    if(verticesIncidentEdge==1) {
      int v1 = t.p[1];
      int v2 = t.p[2]; 
      int v =  verticesToTesselate[*(verticesIncidentEachEdgeOriginalTriangle[1].begin())];          
      
      pair<int,int> candidateEdge;    

      if(v1<v) {candidateEdge.first = v1; candidateEdge.second = v;}
      else {candidateEdge.first = v; candidateEdge.second = v1;}
      statistics.ctEdgesActuallyInsertedInRetesselation++;
      edgesUsedInThisTriangle.insert(candidateEdge);

      if(v2<v) {candidateEdge.first = v2; candidateEdge.second = v;}
      else {candidateEdge.first = v; candidateEdge.second = v2;}
      statistics.ctEdgesActuallyInsertedInRetesselation++;
      edgesUsedInThisTriangle.insert(candidateEdge);
    } else {
      allSimple= false;
    }
  }

  verticesIncidentEdge = verticesIncidentEachEdgeOriginalTriangle[2].size();
  if(verticesIncidentEdge==0) {
    int v1 = t.p[2];
    int v2 = t.p[0];
            
    if(v2<v1) swap(v1,v2); //the first vertex is always smaller than the second in the edges... (to simplify tests)
    pair<int,int> candidateEdge(v1,v2);

    statistics.ctEdgesActuallyInsertedInRetesselation++;
    edgesUsedInThisTriangle.insert(candidateEdge);
  } else {
    if(verticesIncidentEdge==1) {
      int v1 = t.p[2];
      int v2 = t.p[0]; 
      int v =  verticesToTesselate[*(verticesIncidentEachEdgeOriginalTriangle[2].begin())];          
      
      pair<int,int> candidateEdge;    

      if(v1<v) {candidateEdge.first = v1; candidateEdge.second = v;}
      else {candidateEdge.first = v; candidateEdge.second = v1;}
      statistics.ctEdgesActuallyInsertedInRetesselation++;
      edgesUsedInThisTriangle.insert(candidateEdge);

      if(v2<v) {candidateEdge.first = v2; candidateEdge.second = v;}
      else {candidateEdge.first = v; candidateEdge.second = v2;}
      statistics.ctEdgesActuallyInsertedInRetesselation++;
      edgesUsedInThisTriangle.insert(candidateEdge);
    } else {
      allSimple= false;
    }
  }
  

  if(allSimple) statistics.simple++;
  else statistics.difficult++;
  

  if(allSimple) {
    //vertices should connect: internal - boundary , two boundary iff from intersection...

    set<pair<int,int> > internalEdgesThisTriangle;
    for(int edgeId:edgesFromIntersection) {   
      internalEdgesThisTriangle.insert(edgesUsingVertexId[edgeId]);       
    }
    /*set<int> verticesOriginalTriangle;
    for(int i=0;i<3;i++) {
      verticesOriginalTriangle.insert(t.p[i]);
    }*/
    

    for(int i=0;i<numVTriangle;i++) {
      int v1 = verticesToTesselate[i];
      bool v1Original = (v1==t.p[0])||(v1==t.p[1])||(v1==t.p[2]); 
     // bool v1Original = verticesOriginalTriangle.count(v1)!=0; 
      for(int j=i+1;j<numVTriangle;j++) {
        
        int v2 = verticesToTesselate[j];
         
        bool v2Original = (v2==t.p[0])||(v2==t.p[1])||(v2==t.p[2]); 
        if(v1Original && v2Original) continue; //v1 is a boundary vertex...

        pair<int,int> candidateEdge;
        if(v1<v2) {candidateEdge.first = v1; candidateEdge.second = v2;}
        else {candidateEdge.first = v2; candidateEdge.second = v1;}

        if(edgesUsedInThisTriangle.count(candidateEdge)!=0) continue; 

        statistics.ctEdgesTestedToInsertInRetesselation++;

        
        if(!intersects(candidateEdge,internalEdgesThisTriangle,meshWhereTriangleIs,whatPlaneProjectTriangleTo,tempVars)) {        

          statistics.ctEdgesActuallyInsertedInRetesselation++;
          edgesUsedInThisTriangle.insert(candidateEdge); //if it does not intersect, we can safely add this edge to the triangulation...
          internalEdgesThisTriangle.insert(candidateEdge); 
          //cerr << "Dont intersect\n";
        }

      }
    }

  } else { 
    //for each pair of vertices (forming an edge), let's try to add this edge e to the triangulation
    //

    /*set<pair<int,int> > internalEdgesThisTriangle;

    set<int> verticesOriginalTriangle;
    for(int i=0;i<3;i++) {
      verticesOriginalTriangle.insert(t.p[i]);
    } */ 

    for(int i=0;i<numVTriangle;i++) {
      int v1 = verticesToTesselate[i];

     // bool v1Original = verticesOriginalTriangle.count(v1)!=0; 
      for(int j=i+1;j<numVTriangle;j++) {
        
        int v2 = verticesToTesselate[j];


              
        assert(v1<v2); //the vector was previously sorted! also, all elements should be unique!
        //let's try to insert edge (v1,v2)...

        //bool v2Original = verticesOriginalTriangle.count(v2)!=0;


        pair<int,int> candidateEdge(v1,v2);
        if(edgesUsedInThisTriangle.count(candidateEdge)!=0) continue; //the edge was already used...


       /* if(allSimple && v1Original && v2Original) {
          //cerr << "continuing..." << endl;
          continue;
        }*/

        statistics.ctEdgesTestedToInsertInRetesselation++;

        //if edge e=(v1,v2) does not intersect other edges already inserted in this triangle,
        //we will add e to the new triangulation...
        //any triangulation is fine as soon as the edges from the intersection of the meshes are there...
        if(!intersects(candidateEdge,edgesUsedInThisTriangle,meshWhereTriangleIs,whatPlaneProjectTriangleTo,tempVars)) {        


          statistics.ctEdgesActuallyInsertedInRetesselation++;
          edgesUsedInThisTriangle.insert(candidateEdge); //if it does not intersect, we can safely add this edge to the triangulation...
        
          //cerr << "Dont intersect\n";
        } else {
          //cerr << "Intersects\n";
        }
      }
    }
  }


  if(seedEdge.first == -1) { //the edge t.p[0] - t.p[1] is not in the output because other edges intersects it...
    for(const pair<int,int> &e:edgesUsedInThisTriangle) 
      if(e.first == t.p[0] ) { //let's try to find an edge (t.p[0],x) (or (x,t.p[0])) collinear with (t.p[0], t.p[1])
        //is e.second on line (t.p[0]-t.p[1]) ??
        //TODO: this is computed with the projected triangle... we should consider vertical triangles...
        if (orientation(vertices[meshWhereTriangleIs][t.p[0]], vertices[meshWhereTriangleIs][t.p[1]], *getPointFromVertexId(e.second, meshWhereTriangleIs),whatPlaneProjectTriangleTo, tempVars)==0) {
          seedEdge.first = t.p[0];
          seedEdge.second = e.second;
          //cerr << "Aqui" << endl;
          break;
        }
      } else if (e.second == t.p[0]) {
        if (orientation(vertices[meshWhereTriangleIs][t.p[0]], vertices[meshWhereTriangleIs][t.p[1]], *getPointFromVertexId(e.first, meshWhereTriangleIs),whatPlaneProjectTriangleTo,tempVars)==0) {
          seedEdge.first = e.first;
          seedEdge.second = t.p[0];
          //cerr << "Ali" << endl;
          break;
        }
      }         
  }



  createNewTrianglesFromRetesselationAndOrient(edgesUsedInThisTriangle,t,newTrianglesGeneratedFromRetesselation,seedEdge,meshWhereTriangleIs, whatPlaneProjectTriangleTo);
          

  //cerr << meshWhereTriangleIs << " " << i << " " << myNewTrianglesFromRetesselation.size() << endl;
 // for(auto &elem:edgesUsedInThisTriangle)
   // myNewTriEdgesFromEachMap.push_back(elem); ;//newTriEdgesFromEachMap[meshWhereTriangleIs].push_back(elem);   
}


//edges represent the edges generated from the intersection of the intersecting triangles.
//intersectingTrianglesThatGeneratedEdges[i] contains the pair of triangles whose intersection generated edges[i]

void retesselateIntersectingTriangles(const vector< pair< array<VertCoord,3>,array<VertCoord,3> > > &edges, const vector< pair<Triangle *,Triangle *> > &intersectingTrianglesThatGeneratedEdges) {
	assert(edges.size()==intersectingTrianglesThatGeneratedEdges.size());
	timespec t0,t1;
  cerr << "Retesselating triangles..." << "\n";
  clock_gettime(CLOCK_REALTIME, &t0);

  /*long long ctTrianglesRetesselate = 0;
  long long ctVerticesInsertedTrianglesToRetesselate = 0;
  long long ctEdgesTestedToInsertInRetesselation = 0;
  long long ctEdgesActuallyInsertedInRetesselation = 0; //besides the constraint edges (the ones from intersection)
  long long ctEdgesInsertedBecauseOfConstraint = 0; //constraint edges inserted in retesselation*/
  StatisticsAboutRetesseation statisticsAboutRetesseation;

	unordered_map<const Triangle *, vector<int> > intersectingEdgesInEachTriangle[2];
  //trianglesIntersectingEachTriangle[0] --> for each triangle t from map 0 (that intersects other triangles), 
  //trianglesIntersectingEachTriangle[0][t] is a vector of triangles from mesh 1 intersecting t...

  map< Point, int > vertexIdPlusOne; //lets add 1 to the ids (this will simplify the creation of new entries...)
  
  int numEdges = edges.size();
  vector<pair<int,int> > edgesUsingVertexId(numEdges); //if we have an edge (a,b)  --> a<b
  for(int i=0;i<numEdges;i++) {
  	//cerr << "i " << i << " " << numEdges << endl;
  	const auto &e = edges[i];
  	int idA = vertexIdPlusOne[e.first];
  	if(idA==0) {
  		idA = vertexIdPlusOne[e.first] = vertices[2].size()+1; //when we create a new vertex we start counting from 1 (only here!!!)
  		vertices[2].push_back(e.first);
  	}
  	int idB = vertexIdPlusOne[e.second];
  	if(idB==0) {
  		idB = vertexIdPlusOne[e.second] = vertices[2].size()+1;  //when we create a new vertex we start counting from 1 (only here!!!)
  		vertices[2].push_back(e.second);
  	}
  	idA = -idA; //for simplicity, let's use a negative id to referrence a "shared" vertex (from layer 2)
  	idB = -idB;
  	if(idA > idB) swap(idA,idB); 

  	edgesUsingVertexId[i] = make_pair(idA,idB); 

  	//the intersection of tA (from mesh 0) with tB generated the i-th edge...
  	const Triangle *tA = intersectingTrianglesThatGeneratedEdges[i].first;
  	const Triangle *tB = intersectingTrianglesThatGeneratedEdges[i].second;

  	intersectingEdgesInEachTriangle[0][tA].push_back(i);
  	intersectingEdgesInEachTriangle[1][tB].push_back(i);
  }

  clock_gettime(CLOCK_REALTIME, &t1);
  cerr << "Time to extract the edges intersecting each triangle and create the new vertices: " << convertTimeMsecs(diff(t0,t1))/1000 << "\n"; 
 

  //edgesUsingVertexId : each edge contains the ids of its two vertices (idA,idB)
  //idA,idB start with 0 and they represent the position of the vertex in vertices[2].
  //i.e., if an edge is = (idA,idB) --> the two vertices that form this edge are vertices[2][idA],vertices[2][idB]

  //for each triangle t from mesh meshId, intersectingEdgesInEachTriangle[meshId][t] contains a vector of ids of edges formed by the intersection
  //of t with other triangles...


  //TODO: special cases, collinear, etc..

  vector<pair<int,int> > newTriEdgesFromEachMap[2];

  for(int meshIdToProcess=0;meshIdToProcess<2;meshIdToProcess++) {
  	vector< const pair<const Triangle * const, vector<int> >* > intersectingEdgesInEachTriangleToProcess;
  	for(const auto &elem:intersectingEdgesInEachTriangle[meshIdToProcess]) intersectingEdgesInEachTriangleToProcess.push_back(&elem);

  	int numTrianglesToProcess = intersectingEdgesInEachTriangleToProcess.size();

    statisticsAboutRetesseation.ctTrianglesRetesselate += numTrianglesToProcess;

  	#pragma omp parallel 
  	{
  		//vector<pair<int,int> > myNewTriEdgesFromEachMap;

  		vector<TriangleNoBB> myNewTrianglesFromRetesselation;
  		VertCoord tempVars[8];

  		#pragma omp for
		  for(int i=0;i<numTrianglesToProcess;i++) {  		  		
        //for each triangle ts, let's process the triangles intersecting t and the edges
        //formed by the intersection of ts and these triangles

		  	const auto &ts  = *intersectingEdgesInEachTriangleToProcess[i];
		  	//ts.first = a triangle from map 0
		  	//ts.second = list of edges formed by the intersection of ts.first with other triangles...
		  	const auto &t = *ts.first;
		  	const auto &edgesFromIntersection =  ts.second;

        retesselateTriangle(edgesUsingVertexId, edgesFromIntersection,t, meshIdToProcess, myNewTrianglesFromRetesselation, tempVars, statisticsAboutRetesseation);
		  }

		  #pragma omp critical 
		  {
		  	//newTriEdgesFromEachMap[meshIdToProcess].insert( newTriEdgesFromEachMap[meshIdToProcess].end(),myNewTriEdgesFromEachMap.begin(),myNewTriEdgesFromEachMap.end());
				trianglesFromRetesselation[meshIdToProcess].insert(trianglesFromRetesselation[meshIdToProcess].end(),myNewTrianglesFromRetesselation.begin(),myNewTrianglesFromRetesselation.end());
			}

			//break;
		}
	}

	clock_gettime(CLOCK_REALTIME, &t1);
  cerr << "Time to retesselate creating new tri edges: " << convertTimeMsecs(diff(t0,t1))/1000 << "\n";  

  
  cerr << "Counts computed during retesselation: " << "\n";
  statisticsAboutRetesseation.printStatsSoFar();
  cerr << "Number of inters. tests (for insertin tris.)that are true    : " << ctEdgeIntersect << "\n";
  cerr << "Number of inters. tests (for insertin tris.)that are false    : " << ctEdgeDoNotIntersect << "\n";
  
 

  /*
  for(int meshIdToProcess=0;meshIdToProcess<2;meshIdToProcess++) {
  	vector<pair<array<double,3>,array<double,3>> > edgesToStore;

  	for(const pair<int,int> &edge:newTriEdgesFromEachMap[meshIdToProcess]) {
  		const Point *v1 = (edge.first>=0)?&vertices[meshIdToProcess][edge.first]:&vertices[2][-edge.first -1];
  		const Point *v2 = (edge.second>=0)?&vertices[meshIdToProcess][edge.second]:&vertices[2][-edge.second -1];
  		edgesToStore.push_back(make_pair(toDoublePoint(*v1),toDoublePoint(*v2)));
  	}

  	
  	if(meshIdToProcess==0)
  		storeEdgesAsGts("out/retesselatedMesh0.gts",edgesToStore);
  	else 
  		storeEdgesAsGts("out/retesselatedMesh1.gts",edgesToStore);
  }
  */
}



//returns the number of intersections found
//the sets are filled with the triangles (from the corresponding mesh) that intersect
unsigned long long  computeIntersections(const Nested3DGridWrapper *uniformGrid, unordered_set<const Triangle *> trianglesThatIntersect[2]) {
  timespec t0,t1;
  clock_gettime(CLOCK_REALTIME, &t0);
   

  unsigned long long totalIntersections = 0;

  vector<pair<Triangle *,Triangle *> > vtPairsTrianglesToProcess;
  
  
  getPairsTrianglesInSameUnifGridCells(uniformGrid,vtPairsTrianglesToProcess);
  clock_gettime(CLOCK_REALTIME, &t1);
  cerr << "Time creating list of pairs of triangles to process (intersection): " << convertTimeMsecs(diff(t0,t1))/1000 << "\n"; 
  //pairsTrianglesToProcess.reserve(1804900);
  

  //vector<pair<Triangle *,Triangle *> > vtPairsTrianglesToProcess(pairsTrianglesToProcess.begin(),pairsTrianglesToProcess.end());

  int numPairsToTest = vtPairsTrianglesToProcess.size();
  cerr << "Num pairs to test: " << numPairsToTest << endl;
  vector<bool> pairsIntersect(numPairsToTest,false); //true if the corresponding pair intersects..

  unsigned long long totalTests = 0;

  //TODO: try to reduce amount of computation here...

  vector< pair< array<VertCoord,3>,array<VertCoord,3> > > edges;
  vector< pair<Triangle *,Triangle *> >  intersectingTrianglesThatGeneratedEdges;
  #pragma omp parallel
  {
    VertCoord p0InterLine[3],p1InterLine[3];
    VertCoord tempRationals[100];

    vector< pair< array<VertCoord,3>,array<VertCoord,3> > > edgesTemp;
    vector< pair<Triangle *,Triangle *> >  intersectingTrianglesTemp;
    //edgesTemp.reserve(numPairsToTest/10);
    
    unsigned long long totalIntersectionsTemp = 0;
    unsigned long long totalTestsTemp = 0;

    #pragma omp for
    for(int i=0;i<numPairsToTest;i++) {   
      pair<Triangle *,Triangle *> &pairTriangles = vtPairsTrianglesToProcess[i];
      int coplanar;
      Triangle &a = *pairTriangles.first;
      Triangle &b = *pairTriangles.second;

      
      int ans = tri_tri_intersect_with_isectline(vertices[0][a.p[0]].data(),vertices[0][a.p[1]].data(),vertices[0][a.p[2]].data()     ,     
                                                  vertices[1][b.p[0]].data(),vertices[1][b.p[1]].data(),vertices[1][b.p[2]].data(),
                                                  &coplanar, p0InterLine ,p1InterLine,tempRationals);

      totalTestsTemp++;
      if (ans && !coplanar) {
        edgesTemp.push_back( pair<array<VertCoord,3>,array<VertCoord,3>>( {p0InterLine[0],p0InterLine[1],p0InterLine[2]} , {p1InterLine[0],p1InterLine[1],p1InterLine[2]}   ) );
      	intersectingTrianglesTemp.push_back(pairTriangles);
      }

      if(ans) {
        totalIntersectionsTemp++;        
        pairsIntersect[i] = true;
      } 
    }

    #pragma omp critical
    {
      totalIntersections += totalIntersectionsTemp;
      totalTests += totalTestsTemp;
      edges.insert(edges.end(), edgesTemp.begin(), edgesTemp.end());
      intersectingTrianglesThatGeneratedEdges.insert(intersectingTrianglesThatGeneratedEdges.end(), intersectingTrianglesTemp.begin(), intersectingTrianglesTemp.end());
    }
  }
  
  clock_gettime(CLOCK_REALTIME, &t1);
  timeDetectIntersections = convertTimeMsecs(diff(t0,t1))/1000; 




  clock_gettime(CLOCK_REALTIME, &t0);
  retesselateIntersectingTriangles(edges,intersectingTrianglesThatGeneratedEdges);
  clock_gettime(CLOCK_REALTIME, &t1);
  timeRetesselate = convertTimeMsecs(diff(t0,t1))/1000; 



  //Some statistics...

  for(int i=0;i<numPairsToTest;i++)  
  	if(pairsIntersect[i]) {
  		trianglesThatIntersect[0].insert(vtPairsTrianglesToProcess[i].first);
  		trianglesThatIntersect[1].insert(vtPairsTrianglesToProcess[i].second);
  	}



  //TODO: remove this from timing data (this is just a statistic)
  map<const Triangle *,int> ctIntersectionsEachTriangleFromMap[2];
  for(int i=0;i<numPairsToTest;i++)  
  	if(pairsIntersect[i]) {
  		ctIntersectionsEachTriangleFromMap[0][vtPairsTrianglesToProcess[i].first]++;
  		ctIntersectionsEachTriangleFromMap[1][vtPairsTrianglesToProcess[i].second]++;
  	}  

  int ctTrianglesIntersect[2]  = {0,0};	
  int sumIntersections[2] = {0,0};	
  int maxIntersections[2] = {0,0};
  for(int meshId=0;meshId<2;meshId++) {
  	for(auto &a:ctIntersectionsEachTriangleFromMap[meshId]) {
  		if(a.second>maxIntersections[meshId])
  			maxIntersections[meshId] = a.second;
  		ctTrianglesIntersect[meshId]++;
  		sumIntersections[meshId]+= a.second;
  	}
  	cerr << "Mesh: " << meshId << endl;
  	cerr << "Max intersections                 : " << maxIntersections[meshId] << endl;
  	cerr << "Total intersections               : " << sumIntersections[meshId] << endl;
  	cerr << "Mum triangles intersecting        : " << ctTrianglesIntersect[meshId] << endl;
  	cerr << "Average intersections per triangle: " << sumIntersections[meshId]*1.0/ctTrianglesIntersect[meshId] << endl;
  }


  cerr << "Total tests: " << totalTests << endl;
  cerr << "Total intersections: " << totalIntersections << endl;


  //for debugging/visualization, let's store in a file for each triangle the original edges of that triangle and 
  //the edges formed by the intersection of other triangles...

  vector<pair<Triangle *,Triangle *> > pairsIntersectingTriangles;
  for(int i=0;i<numPairsToTest;i++)  
  	if(pairsIntersect[i]) {
  		pairsIntersectingTriangles.push_back(vtPairsTrianglesToProcess[i]);
  	}
  storeTriangleIntersections(pairsIntersectingTriangles); // TODO: use the edges vector (we already have the edges from intersections!!!)


           
  return totalIntersections;
}



//TODO: if all the tree vertices of the original triangle are in the same grid cell --> the new triangles will also be!

//----------------------------------------------------------------------------
// TODO: not all vertices from the retesselated triangles will be in the output (?)... we do not need to store them!
/*//Each vector represents the vertices of a layer
vector<Point> vertices[2];

//Each vector represents a set of objects in the same layer
//The objects are represented by a set of triangles (defining their boundaries)
vector<Triangle> triangles[2]; //
*/
void classifyTrianglesAndGenerateOutput(const Nested3DGridWrapper *uniformGrid, const unordered_set<const Triangle *> trianglesThatIntersect[2],ostream &outputStream) {
	timespec t0,t0ThisFunction,t1;
	clock_gettime(CLOCK_REALTIME, &t0);
	t0ThisFunction = t0;

	int ctIntersectingTrianglesTotal =0;
	vector<Triangle> outputTriangles[2]; //output triangles generated from triangles of each mesh
	vector<TriangleNoBB> outputTrianglesFromRetesselation[2];
	for(int meshId=0;meshId<2;meshId++){
    timespec t0,t1;
    clock_gettime(CLOCK_REALTIME, &t0);

		vector<Point *> verticesToLocateInOtherMesh;


		vector<int> global_x_coord_vertex_to_locate,global_y_coord_vertex_to_locate,global_z_coord_vertex_to_locate;
		for(const Triangle&t:triangles[meshId]) {
			if(trianglesThatIntersect[meshId].count(&t)==0) { //this triangle does not intersect the other mesh...
				verticesToLocateInOtherMesh.push_back(&(vertices[meshId][t.p[0]]));			
				global_x_coord_vertex_to_locate.push_back(uniformGrid->get_global_x_coord_mesh_vertex(meshId,t.p[0]));	
				global_y_coord_vertex_to_locate.push_back(uniformGrid->get_global_y_coord_mesh_vertex(meshId,t.p[0]));	
				global_z_coord_vertex_to_locate.push_back(uniformGrid->get_global_z_coord_mesh_vertex(meshId,t.p[0]));	
			} else {
				ctIntersectingTrianglesTotal++;
			}
		}



		int posStartVerticesOfIntersectingTrianglesInThisMesh = verticesToLocateInOtherMesh.size();
		const int numIntersectingT = trianglesFromRetesselation[meshId].size();
		cerr << "Mesh " << meshId << " Num tri from retesselation: " << numIntersectingT << endl;
		vector<Point> centerOfIntersectingTriangles(numIntersectingT);
		

    const int numVerticesLocateFromNonIntersectingTriangles = verticesToLocateInOtherMesh.size();
    //Resize all vectors to fit the data related to the intersecting triangles...
    global_x_coord_vertex_to_locate.resize(numVerticesLocateFromNonIntersectingTriangles+numIntersectingT);
    global_y_coord_vertex_to_locate.resize(numVerticesLocateFromNonIntersectingTriangles+numIntersectingT);
    global_z_coord_vertex_to_locate.resize(numVerticesLocateFromNonIntersectingTriangles+numIntersectingT);
    verticesToLocateInOtherMesh.resize(numVerticesLocateFromNonIntersectingTriangles+numIntersectingT);


    clock_gettime(CLOCK_REALTIME, &t0);


		#pragma omp parallel 
		{
			VertCoord tempVar;
      big_int tempVarsInt[3];

      #pragma omp for
			for(int tid=0;tid<numIntersectingT;tid++) {
			 	const TriangleNoBB& t = trianglesFromRetesselation[meshId][tid];
				Point *a = getPointFromVertexId(t.p[0], meshId);
				Point *b = getPointFromVertexId(t.p[1], meshId);
				Point *c = getPointFromVertexId(t.p[2], meshId);
				
				for(int i=0;i<3;i++) centerOfIntersectingTriangles[tid][i] = 0;
				for(int i=0;i<3;i++) {
					centerOfIntersectingTriangles[tid][i] += (*a)[i];
					centerOfIntersectingTriangles[tid][i] += (*b)[i];
					centerOfIntersectingTriangles[tid][i] += (*c)[i];
					centerOfIntersectingTriangles[tid][i] /= 3;
				}
				verticesToLocateInOtherMesh[tid+numVerticesLocateFromNonIntersectingTriangles] = (&centerOfIntersectingTriangles[tid]);
				global_x_coord_vertex_to_locate[tid+numVerticesLocateFromNonIntersectingTriangles] = (uniformGrid->x_global_cell_from_coord(centerOfIntersectingTriangles[tid][0], tempVar,tempVarsInt));	
				global_y_coord_vertex_to_locate[tid+numVerticesLocateFromNonIntersectingTriangles] = (uniformGrid->y_global_cell_from_coord(centerOfIntersectingTriangles[tid][1], tempVar,tempVarsInt));	
				global_z_coord_vertex_to_locate[tid+numVerticesLocateFromNonIntersectingTriangles] = (uniformGrid->z_global_cell_from_coord(centerOfIntersectingTriangles[tid][2], tempVar,tempVarsInt));	
			}
		}


		//TODO: locate unique vertices???
		vector<ObjectId> locationOfEachVertexInOtherMesh(verticesToLocateInOtherMesh.size());
		//vertices of mesh "meshId" will be located in mesh "1-meshId"

    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to get grid coord of vertices from intersection (preparing for classification): " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
		
		clock_gettime(CLOCK_REALTIME, &t0);
		locateVerticesInObject(uniformGrid,  verticesToLocateInOtherMesh,global_x_coord_vertex_to_locate,global_y_coord_vertex_to_locate,global_z_coord_vertex_to_locate,locationOfEachVertexInOtherMesh,1-meshId);
		clock_gettime(CLOCK_REALTIME, &t1);
  	cerr << "Total time to locate: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
		//now, we know in what object of the other mesh each triangle that does not intersect other triangles is...


		int numTrianglesThisMesh = triangles[meshId].size();
		int ctTrianglesProcessed = 0;


		for(int i=0;i<numTrianglesThisMesh;i++) {
			const Triangle &t=triangles[meshId][i];
			if(trianglesThatIntersect[meshId].count(&t)==0) { //this triangle does not intersect the other mesh...
				//this will (probably) be an output triangle...
				ObjectId objWhereTriangleIs = locationOfEachVertexInOtherMesh[ctTrianglesProcessed++];		
				//cerr << "obj: " << objWhereTriangleIs << endl;		
				if (objWhereTriangleIs!=OUTSIDE_OBJECT) {
					//if the triangle is not outside the other mesh, it will be in the output (we still need to update the left/right objects correctly...)
					outputTriangles[meshId].push_back(t);
				}				
			}			
		}


		int numTrianglesFromIntersectionThisMesh = trianglesFromRetesselation[meshId].size();

		
		for(int i=0;i<numTrianglesFromIntersectionThisMesh;i++) {
			const TriangleNoBB&t = trianglesFromRetesselation[meshId][i];
			ObjectId objWhereTriangleIs = locationOfEachVertexInOtherMesh[ctTrianglesProcessed++];		
			
			
			if (objWhereTriangleIs!=OUTSIDE_OBJECT) {
				/*if(meshId==1) {
				cerr << centerOfIntersectingTriangles[i][0].get_d() << " " << centerOfIntersectingTriangles[i][1].get_d() << " " <<  centerOfIntersectingTriangles[i][2].get_d() << endl;
				cerr << "Obj: " << objWhereTriangleIs << endl << endl;
				}*/

					//if the triangle is not outside the other mesh, it will be in the output (we still need to update the left/right objects correctly...)
					outputTrianglesFromRetesselation[meshId].push_back(t);
			}								
		}
	}
	clock_gettime(CLOCK_REALTIME, &t1);
  cerr << "Time to locate vertices and classify triangles: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
  Print_Current_Process_Memory_Used();	
  clock_gettime(CLOCK_REALTIME, &t0); 
  
	vector<bool> verticesToWriteOutputMesh0(vertices[0].size(),false);
	vector<int> newIdVerticesMesh0(vertices[0].size(),-1);
	for(const Triangle &t : outputTriangles[0]) {
		verticesToWriteOutputMesh0[t.p[0]] = verticesToWriteOutputMesh0[t.p[1]] = verticesToWriteOutputMesh0[t.p[2]] = true;
	}
	for(TriangleNoBB &t : outputTrianglesFromRetesselation[0]) {
		//cerr << t.p[0] << " " << vertices[0].size() << endl;
		assert(t.p[0] < (int)vertices[0].size());
		assert(t.p[1] < (int)vertices[0].size());
		assert(t.p[2] < (int)vertices[0].size());

		if(t.p[0]>=0) verticesToWriteOutputMesh0[t.p[0]] = true;
		if(t.p[1]>=0) verticesToWriteOutputMesh0[t.p[1]] = true;
		if(t.p[2]>=0) verticesToWriteOutputMesh0[t.p[2]] = true;
	}
	int numVerticesMesh0InOutput = 0;
	int sz = vertices[0].size();
	for(int i=0;i<sz;i++) 
		if(verticesToWriteOutputMesh0[i]) {			
			newIdVerticesMesh0[i] = numVerticesMesh0InOutput;
			numVerticesMesh0InOutput++;
		}

	vector<bool> verticesToWriteOutputMesh1(vertices[1].size(),false);
	vector<int> newIdVerticesMesh1(vertices[1].size(),-1);
	for(const Triangle &t : outputTriangles[1]) {
		verticesToWriteOutputMesh1[t.p[0]] = verticesToWriteOutputMesh1[t.p[1]] = verticesToWriteOutputMesh1[t.p[2]] = true;
	}
	for(TriangleNoBB &t : outputTrianglesFromRetesselation[1]) {
		assert(t.p[0] < (int)vertices[1].size());
		assert(t.p[1] < (int)vertices[1].size());
		assert(t.p[2] < (int)vertices[1].size());
		if(t.p[0]>=0) verticesToWriteOutputMesh1[t.p[0]] = true;
		if(t.p[1]>=0) verticesToWriteOutputMesh1[t.p[1]] = true;
		if(t.p[2]>=0) verticesToWriteOutputMesh1[t.p[2]] = true;
	}

	int numVerticesMesh1InOutput = 0;
	sz = vertices[1].size();
	for(int i=0;i<sz;i++) 
		if(verticesToWriteOutputMesh1[i]) {			
			newIdVerticesMesh1[i] = numVerticesMesh0InOutput + numVerticesMesh1InOutput;
			numVerticesMesh1InOutput++;
		}

	//compute the new ids of the shared vertices...

	for(Triangle &t : outputTriangles[0]) {
		t.p[0] = newIdVerticesMesh0[t.p[0]];
		t.p[1] = newIdVerticesMesh0[t.p[1]];
		t.p[2] = newIdVerticesMesh0[t.p[2]];
	}
	for(Triangle &t : outputTriangles[1]) {
		t.p[0] = newIdVerticesMesh1[t.p[0]];
		t.p[1] = newIdVerticesMesh1[t.p[1]];
		t.p[2] = newIdVerticesMesh1[t.p[2]];
	}

	
	//the new ids of the shared vertices will be i+numVerticesMesh0InOutput + numVerticesMesh1InOutput, for i = 0...num shared vertices-1.
	const int baseIdsSharedVertices = numVerticesMesh0InOutput + numVerticesMesh1InOutput -1;

	//cerr << baseIdsSharedVertices << endl;
	//cerr << newIdVerticesMesh0.size() << endl;
	//cerr << outputTrianglesFromRetesselation[0][351].p[2] << endl;
	for(TriangleNoBB &t : outputTrianglesFromRetesselation[0]) {
		t.p[0] = (t.p[0]<0)?baseIdsSharedVertices-t.p[0]:newIdVerticesMesh0[t.p[0]];
		t.p[1] = (t.p[1]<0)?baseIdsSharedVertices-t.p[1]:newIdVerticesMesh0[t.p[1]];
		t.p[2] = (t.p[2]<0)?baseIdsSharedVertices-t.p[2]:newIdVerticesMesh0[t.p[2]];
	}
	//cerr << outputTrianglesFromRetesselation[0][351].p[2] << endl;
	//
	for(TriangleNoBB &t : outputTrianglesFromRetesselation[1]) {
		t.p[0] = (t.p[0]<0)?baseIdsSharedVertices-t.p[0]:newIdVerticesMesh1[t.p[0]];
		t.p[1] = (t.p[1]<0)?baseIdsSharedVertices-t.p[1]:newIdVerticesMesh1[t.p[1]];
		t.p[2] = (t.p[2]<0)?baseIdsSharedVertices-t.p[2]:newIdVerticesMesh1[t.p[2]];
	}
	

	clock_gettime(CLOCK_REALTIME, &t1);
  cerr << "Time to update ids of new vertices: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
  Print_Current_Process_Memory_Used();	
  clock_gettime(CLOCK_REALTIME, &t0); 

  int totalNumberOutputVertices = numVerticesMesh0InOutput+numVerticesMesh1InOutput + vertices[2].size();
  int totalNumberOutputVerticesFromNonIntersectingTriangles = numVerticesMesh0InOutput+numVerticesMesh1InOutput;
  int totalNumberOutputTriangles = outputTriangles[0].size() + outputTriangles[1].size() + outputTrianglesFromRetesselation[0].size() + outputTrianglesFromRetesselation[1].size();

  
  unordered_map<pair<int,int>,int> edgesIds; //maybe use unordered_map for performance (if necessary...)
  vector<pair<int,int> > outputEdges;
 
	//vector<pair<int,int> > outputEdgesWithRepetition[2];

	for(int meshId=0;meshId<2;meshId++) {
		int numNewEdgesToAdd =0;
		#pragma omp parallel
		{			
			const int sz = 	outputTriangles[meshId].size();
			vector<pair<int,int> > myEdgesFound;	
			

			#pragma omp for
		  for(int i=0;i<sz;i++) {
		  	const Triangle &t = outputTriangles[meshId][i];
				int a = t.p[0];
				int b = t.p[1];
				int c = t.p[2];

				pair<int,int> e;
				if (a<b) {e.first = a; e.second = b;}
				else     {e.first = b; e.second = a;}
				myEdgesFound.push_back(e);

				if (b<c) {e.first = b; e.second = c;}
				else     {e.first = c; e.second = b;}
				myEdgesFound.push_back(e);

				if (c<a) {e.first = c; e.second = a;}
				else     {e.first = a; e.second = c;}
				myEdgesFound.push_back(e);
			}

			const int szOutputTrianglesFromIntersection = outputTrianglesFromRetesselation[meshId].size();
			#pragma omp for
		  for(int i=0;i<szOutputTrianglesFromIntersection;i++) {
		  	const TriangleNoBB &t = outputTrianglesFromRetesselation[meshId][i];
				int a = t.p[0];
				int b = t.p[1];
				int c = t.p[2];
			//	cerr << meshId << " " << i << " "  << szOutputTrianglesFromIntersection << " " << a << " " << b << " " << c << endl;
				assert(a>=0);
				assert(b>=0);
				assert(c>=0);
				
				pair<int,int> e;
				if (a<b) {e.first = a; e.second = b;}
				else     {e.first = b; e.second = a;}
				myEdgesFound.push_back(e);

				if (b<c) {e.first = b; e.second = c;}
				else     {e.first = c; e.second = b;}
				myEdgesFound.push_back(e);

				if (c<a) {e.first = c; e.second = a;}
				else     {e.first = a; e.second = c;}
				myEdgesFound.push_back(e);
			}


			sort(myEdgesFound.begin(),myEdgesFound.end());
			auto newEndItr = unique(myEdgesFound.begin(),myEdgesFound.end());
			myEdgesFound.resize(newEndItr- myEdgesFound.begin());

			

			#pragma omp critical
			{
				outputEdges.insert(outputEdges.end(),myEdgesFound.begin(),myEdgesFound.end());			
			}
		}
	}
	sort(outputEdges.begin(),outputEdges.end());
	auto newEndItr = unique(outputEdges.begin(),outputEdges.end());
	outputEdges.resize(newEndItr- outputEdges.begin());

	clock_gettime(CLOCK_REALTIME, &t1);
	cerr << "T so far: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;

	
	int totalNumberOutputEdges = outputEdges.size();
	vector<map<int,int> > mapEdgesIds2(totalNumberOutputVertices); //maps each edge (pair of vertices) to ids id
	for (int i=0;i<totalNumberOutputEdges;i++) {
		const pair<int,int> &e = outputEdges[i];
		//edgesIds[e] = i;

		assert(e.first>=0 );
		assert(e.second>=0);
		assert(e.first<totalNumberOutputVertices);
		//cerr << e.first << " " << e.second << " " << totalNumberOutputEdges << endl;
		assert(e.second<totalNumberOutputVertices);		
		mapEdgesIds2[e.first][e.second] = i;
	}

	clock_gettime(CLOCK_REALTIME, &t1);
	timeClassifyTriangles = convertTimeMsecs(diff(t0ThisFunction,t1))/1000;

  cerr << "Time to create edges: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
  cerr << "Total time (excluding I/O) so far: " << convertTimeMsecs(diff(t0AfterDatasetRead,t1))/1000 << endl;
  Print_Current_Process_Memory_Used();	
  clock_gettime(CLOCK_REALTIME, &t0); 

  

  
 

  //now, let's write everything in the output!
  outputStream << totalNumberOutputVertices << " " << totalNumberOutputEdges << " " << totalNumberOutputTriangles << '\n';

  //print the coordinates of the vertices...
  sz = verticesToWriteOutputMesh0.size();
  for(int i=0;i<sz;i++) 
		if(verticesToWriteOutputMesh0[i]) 
			outputStream << vertices[0][i][0].get_d() << " " << vertices[0][i][1].get_d() << " " << vertices[0][i][2].get_d() << "\n";
	sz = verticesToWriteOutputMesh1.size();
  for(int i=0;i<sz;i++) 
		if(verticesToWriteOutputMesh1[i]) 
			outputStream << vertices[1][i][0].get_d() << " " << vertices[1][i][1].get_d() << " " << vertices[1][i][2].get_d() << "\n";
	sz = vertices[2].size();
  for(int i=0;i<sz;i++) 
			outputStream << vertices[2][i][0].get_d() << " " << vertices[2][i][1].get_d() << " " << vertices[2][i][2].get_d() << "\n";		

	//print edges...
	for(const pair<int,int> &p:outputEdges) {
		outputStream << p.first+1 << " " << p.second+1 << "\n"; //in a GTS file we start counting from 1...
	}

	//print triangles...
	for(int meshId=0;meshId<2;meshId++) 
		for(Triangle &t : outputTriangles[meshId]) {
			int a = t.p[0];
			int b = t.p[1];
			int c = t.p[2];


			if(t.above != OUTSIDE_OBJECT) {
				//according to the right hand rule, the ordering of the vertices should be (c,b,a)
				swap(a,c);
			} 
			pair<int,int> e;
			if (a<b) {e.first = a; e.second = b;}
			else     {e.first = b; e.second = a;}
			//outputStream << edgesIds[e]+1 << " "; //we start counting from 1 in GTS files...
			outputStream << mapEdgesIds2[e.first][e.second]+1 << " ";

			if (b<c) {e.first = b; e.second = c;}
			else     {e.first = c; e.second = b;}
			//outputStream << edgesIds[e]+1 << " ";
			outputStream << mapEdgesIds2[e.first][e.second]+1 << " ";

			if (c<a) {e.first = c; e.second = a;}
			else     {e.first = a; e.second = c;}
			//outputStream << edgesIds[e]+1 << "\n";
			outputStream << mapEdgesIds2[e.first][e.second]+1 << "\n";
		}

		for(int meshId=0;meshId<2;meshId++) 
			for(TriangleNoBB &t : outputTrianglesFromRetesselation[meshId]) {
				int a = t.p[0];
				int b = t.p[1];
				int c = t.p[2];

				//cerr << a << " " << b << " " << c << endl;
				if(a<0) { //if the vertex refers to a shared vertex (created from the intersection)
					a = -a -1 + totalNumberOutputVerticesFromNonIntersectingTriangles;
				}
				if(b<0) {
					b = -b -1 + totalNumberOutputVerticesFromNonIntersectingTriangles;
				}
				if(c<0) {
					c = -c -1 + totalNumberOutputVerticesFromNonIntersectingTriangles;
				}

				if(t.above != OUTSIDE_OBJECT) {
					//according to the right hand rule, the ordering of the vertices should be (c,b,a)
					swap(a,c);
				} 
				pair<int,int> e;
				if (a<b) {e.first = a; e.second = b;}
				else     {e.first = b; e.second = a;}
				//outputStream << edgesIds[e]+1 << " "; //we start counting from 1 in GTS files...
				outputStream << mapEdgesIds2[e.first][e.second]+1 << " ";

				if (b<c) {e.first = b; e.second = c;}
				else     {e.first = c; e.second = b;}
				//outputStream << edgesIds[e]+1 << " ";
				outputStream << mapEdgesIds2[e.first][e.second]+1 << " ";

				if (c<a) {e.first = c; e.second = a;}
				else     {e.first = a; e.second = c;}
				//outputStream << edgesIds[e]+1 << "\n";
				outputStream << mapEdgesIds2[e.first][e.second]+1 << "\n";
			}
	
		cerr << "Output vertices         : " << totalNumberOutputVertices << endl;
		cerr << "Output edges            : " << totalNumberOutputEdges << endl;
		cerr << "Output triangles non int: " << totalNumberOutputTriangles << endl;
		cerr << "Intersecting triangles  : " << ctIntersectingTrianglesTotal << endl;

}


void readInputMesh(int meshId, string path) {
	assert(path.size()>=4);
  string inputFileExtension = path.substr(path.size()-3,3);
  assert(inputFileExtension=="gts" || inputFileExtension=="ium");
  bool isGtsFile = inputFileExtension=="gts";
  if(isGtsFile)
    TIME(readGTSFile(path,vertices[meshId],triangles[meshId],bBox[meshId]));
  else 
    TIME(readLiumFile(path,vertices[meshId],triangles[meshId],bBox[meshId]));
}




// TODO: if(a*b > 0 ) --> do not multiply!!! use test a>>0 && b>0 || a<0 && b<0

int main(int argc, char **argv) {
  if (argc!=7) {
      cerr << "Error... use ./3dIntersection inputMesh0 inputMesh1 gridSizeLevel1 gridSizeLevel2 triggerSecondLevel outputFile.gts" << endl;
      cerr << "The mesh file may be in the gts format or in the lium format (multimaterial)" << endl;
      exit(1);
  }
  clock_gettime(CLOCK_REALTIME, &t0BeginProgram);

  string mesh0Path = argv[1];
  string mesh1Path = argv[2];
  int gridSizeLevel1 = atoi(argv[3]);
  int gridSizeLevel2 = atoi(argv[4]);
  int triggerSecondLevel = atoi(argv[5]);
  int maxTreeDepth = 2;

  Print_Current_Process_Memory_Used();
  cerr << "Reading meshes..." << endl;

  timespec t0,t1;
  clock_gettime(CLOCK_REALTIME, &t0); 

  readInputMesh(0,mesh0Path);
  readInputMesh(1,mesh1Path);

  clock_gettime(CLOCK_REALTIME, &t1);
  t0AfterDatasetRead = t1;
  cerr << "Time to read: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
  timeReadData = convertTimeMsecs(diff(t0,t1))/1000; 
  Print_Current_Process_Memory_Used();


  boundingBoxTwoMeshesTogetter[0] = bBox[0][0];
  boundingBoxTwoMeshesTogetter[1] = bBox[0][1];

  for(int i=0;i<3;i++) {
  	if(bBox[1][0][i] < boundingBoxTwoMeshesTogetter[0][i])
  		boundingBoxTwoMeshesTogetter[0][i] = bBox[1][0][i];
  	if(bBox[1][1][i] > boundingBoxTwoMeshesTogetter[1][i])
  		boundingBoxTwoMeshesTogetter[1][i] = bBox[1][1][i];
  }
  

  

  

  

  for(int meshId=0;meshId<2;meshId++)
  	cerr << "Bounding box mesh " << meshId << ": " << setprecision(18) << std::fixed << bBox[meshId][0][0].get_d() << " " << bBox[meshId][0][1].get_d() <<  " " << bBox[meshId][0][2].get_d() << " -- " << bBox[meshId][1][0].get_d() << " " << bBox[meshId][1][1].get_d() << " " << bBox[meshId][1][2].get_d() <<endl;
  cerr << "Bounding box two meshes togetter " << ": " << setprecision(18) << std::fixed << boundingBoxTwoMeshesTogetter[0][0].get_d() << " " << boundingBoxTwoMeshesTogetter[0][1].get_d() <<  " " << boundingBoxTwoMeshesTogetter[0][2].get_d() << " -- " << boundingBoxTwoMeshesTogetter[1][0].get_d() << " " << boundingBoxTwoMeshesTogetter[1][1].get_d() << " " << boundingBoxTwoMeshesTogetter[1][2].get_d() <<endl;


  
  cerr <<"Creating nested grid..." << endl;

  clock_gettime(CLOCK_REALTIME, &t0); 
  vector<Triangle *> trianglesPointers[2];

  int sz = triangles[0].size();
  trianglesPointers[0].resize(sz);
  for(int i=0;i<sz;i++) trianglesPointers[0][i]  = & (triangles[0][i]);

  sz = triangles[1].size();
  trianglesPointers[1].resize(sz);
  for(int i=0;i<sz;i++) trianglesPointers[1][i]  = & (triangles[1][i]);	


//  void init(const vector<Triangle *> trianglesInsert[2], const vector<Point> vertices[2], const int gridSizeLevel1, const int gridSizeLevel2, const Point &p0, const Point &p1,const long long prodThreshold);

  TIME(uniformGrid.init(trianglesPointers, vertices, gridSizeLevel1,gridSizeLevel2,boundingBoxTwoMeshesTogetter[0],boundingBoxTwoMeshesTogetter[1],triggerSecondLevel));


  clock_gettime(CLOCK_REALTIME, &t1);
  cerr << "Time to create and refine grid: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
  timeCreateGrid = convertTimeMsecs(diff(t0,t1))/1000; 

  //After the uniform grid is initialized, let's compute the intersection between the triangles...
  cerr << "Detecting intersections..." << endl;
  clock_gettime(CLOCK_REALTIME, &t0); 
  unordered_set<const Triangle *> trianglesThatIntersect[2];
  unsigned long long numIntersectionsDetected = computeIntersections(&uniformGrid,trianglesThatIntersect);

  clock_gettime(CLOCK_REALTIME, &t1);
  cerr << "Time to detect intersections (includes time for computing statistics and for saving intersections for debugging): " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
  Print_Current_Process_Memory_Used();



  clock_gettime(CLOCK_REALTIME, &t0); 

  ofstream outputStream(argv[6]);
  assert(outputStream);
  classifyTrianglesAndGenerateOutput(&uniformGrid,trianglesThatIntersect,outputStream);

  clock_gettime(CLOCK_REALTIME, &t1);
  cerr << "Total time to classify triangles and generate output: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
  Print_Current_Process_Memory_Used();

  cerr << "----------------------------------------------------" << endl;
  cerr << "Summary of ACTUAL times (excluding the times to compute statistics, to write edges for debugging, etc): " << endl;
  cerr << "Time to read the data         : " << timeReadData << endl;
  cerr << "Time to create and refine grid: " << timeCreateGrid << endl;
  cerr << "Time to detect intersections  : " << timeDetectIntersections << endl;  
  cerr << "Time to retesselate trinagles : " << timeRetesselate << endl;
  cerr << "Time to classify the triangles: " << timeClassifyTriangles << endl;
  cerr << "----------------------------------------------------" << endl;
/*
  if(isGtsFile)
    for(const ObjectId& id:pointIds) {
      cout << id << "\n";
    }
  else
    for(const ObjectId& id:pointIds) {  //when we deal with LIUM files the -1 represents the outside object and objects start with id 0.... (we convert from -1 to 0 and from 0 to 1 in the input... so, we need to revert this change...s)
        cout << id-1 << "\n";
    } 
*/
}