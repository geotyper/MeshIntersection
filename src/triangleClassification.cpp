#include <queue>

#include "triangleClassification.h"


const int DONT_KNOW_FLAG = -999999999;


#include "PinMesh.h"




int labelConnectedComponentsEachVertex(const vector<vector<int> > &adjList,vector<int> &connectedComponentEachVertex,vector<int> &sampleVertexIdFromEachConnectedComponent) {
  int numComponentsSeenSoFar = 0;
  queue< int >   vertexToLabel;
  int numVertices = adjList.size();

  vector<bool> shouldLabel(adjList.size(),true);
  for(int i=0;i<numVertices;i++) 
    if(shouldLabel[i]) {
      vertexToLabel.push(i);
      connectedComponentEachVertex[i] = numComponentsSeenSoFar;
      shouldLabel[i] = false;
      sampleVertexIdFromEachConnectedComponent.push_back(i);

      while(!vertexToLabel.empty()) {
        int v = vertexToLabel.front();
        vertexToLabel.pop(); 
        

        for(int neighbor:adjList[v]) {
          if(shouldLabel[neighbor]) {
            shouldLabel[neighbor] = false;
            connectedComponentEachVertex[neighbor] = numComponentsSeenSoFar;
            vertexToLabel.push(neighbor);
          }
        }
      }

      numComponentsSeenSoFar++;
    }
   

  return numComponentsSeenSoFar;
}

#include "unionFind.h"
int labelConnectedComponentsEachVertex(const vector<pair<int,int> > &adjList,const vector<pair<int,int> > &raggedArray,vector<int> &connectedComponentEachVertex,vector<int> &sampleVertexIdFromEachConnectedComponent) {
  int numVertices = adjList.size();
  DisjointSets djSet(numVertices);

  #pragma omp parallel for
  for(int i=0;i<numVertices;i++) 
    for(int j=adjList[i].first;j<adjList[i].second;j++) {
          int neighbor = raggedArray[j].second;
          djSet.unite(i,neighbor);
    }

  int ct =0;
  unordered_map<int,int> connectedComponentToNumberFrom0;

  #pragma omp parallel for
  for(int i=0;i<numVertices;i++) {
    int idI = djSet.find(i); //id of the connected component of the vertex...
    if(connectedComponentToNumberFrom0.count(idI)==0) { //is this connected component already "registered"?
      #pragma omp critical
      if(connectedComponentToNumberFrom0.count(idI)==0) {
        connectedComponentToNumberFrom0[idI] = ct;
        sampleVertexIdFromEachConnectedComponent.push_back(i);
        ct++;
      }
    }   
  }  

  #pragma omp parallel for
  for(int i=0;i<numVertices;i++) {
    int idI = djSet.find(i);
    int cc = connectedComponentToNumberFrom0[idI];
    connectedComponentEachVertex[i] = cc;
  }

  assert(ct == sampleVertexIdFromEachConnectedComponent.size());
  return ct;
}

int labelConnectedComponentsEachVertex2(const vector<pair<int,int> > &adjList,const vector<pair<int,int> > &raggedArray,vector<int> &connectedComponentEachVertex,vector<int> &sampleVertexIdFromEachConnectedComponent) {
  int numComponentsSeenSoFar = 0;
  queue< int >   vertexToLabel;
  int numVertices = adjList.size();

  vector<bool> shouldLabel(numVertices,true);
  for(int i=0;i<numVertices;i++) 
    if(shouldLabel[i]) {
      vertexToLabel.push(i);
      connectedComponentEachVertex[i] = numComponentsSeenSoFar;
      shouldLabel[i] = false;
      sampleVertexIdFromEachConnectedComponent.push_back(i);

      while(!vertexToLabel.empty()) {
        int v = vertexToLabel.front();
        vertexToLabel.pop(); 
        

        for(int j=adjList[v].first;j<adjList[v].second;j++) {
          int neighbor = raggedArray[j].second;
          if(shouldLabel[neighbor]) {
            shouldLabel[neighbor] = false;
            connectedComponentEachVertex[neighbor] = numComponentsSeenSoFar;
            vertexToLabel.push(neighbor);
          }
        }
      }

      numComponentsSeenSoFar++;
    }
   

  return numComponentsSeenSoFar;
}




//Checks if at least one of the vertices in the interval is not shared between the two meshes
//Returns a pointer for the first vertex (pointer to the first vertex id) that is non shared
//(or NULL if there is no such a vertex)
//[begin,end) is the interval of vertices representing the polygon
const Vertex* getNonSharedVertextFromPolygon(const Vertex * const*begin,const Vertex * const*end) {
  while(begin!=end) {
    if(!(*begin)->isFromIntersection()) return *begin;
    begin++;
  }
  return NULL;  
}



void locatePolygonsOtherMeshUsingDFS(const MeshIntersectionGeometry &geometry,
                                    pair<const InputTriangle *,vector<BoundaryPolygon>> &tri) {
  stack< BoundaryPolygon * > bpToProcess;

  for(BoundaryPolygon&bp:tri.second) {
    if(bp.getPolyhedronWherePolygonIs()!=DONT_KNOW_ID) { //do we know the location of this polygon?
      bpToProcess.push(&bp);
    }    
  }

  assert(!bpToProcess.empty()); //at least one polygon should be known (at least the polygons containing the input vertices of the triangle...)

  while(!bpToProcess.empty()) {
    BoundaryPolygon *bp = bpToProcess.top();
    bpToProcess.pop();

    //cerr << "Found one with id... " << bp->getPolyhedronWherePolygonIs() << endl;
    //for each edge, let's add the neighbor (if it was not processed yet...)
    const int numEdgesBp = bp->boundaryPolygonOtherSideEdge.size();
    for(int i=0;i<numEdgesBp;i++) 
      if(bp->boundaryPolygonOtherSideEdge[i]!=NULL) { //is there anything on the other side? (anything belonging to this triangle?)
        ObjectId objectThisMesh = bp->getPolyhedronWherePolygonIs();
        pair<ObjectId,ObjectId> &objectsBoundedByThisEdge = bp->objectsOtherMeshBoundedByThisEdge[i];

        ObjectId objectOtherPolygon;
        if(objectsBoundedByThisEdge.first==DONT_KNOW_ID) {
          objectOtherPolygon = objectThisMesh; //the edge is not an edge from intersection --> both polygons are in the same object...
        } else {
          objectOtherPolygon = (objectThisMesh==objectsBoundedByThisEdge.first)?objectsBoundedByThisEdge.second:objectsBoundedByThisEdge.first;
          assert(objectOtherPolygon!=objectThisMesh); //the edge should separate different objects...
        }
        BoundaryPolygon *bpOtherSide = bp->boundaryPolygonOtherSideEdge[i];
        if(bpOtherSide->getPolyhedronWherePolygonIs()==DONT_KNOW_ID) { //not inserted into the stack yet...
          bpOtherSide->setPolyhedronWherePolygonIs(objectOtherPolygon);
          bpToProcess.push(bpOtherSide); //let's process it latter (DFS...)
        } else {
          //already labeled... should be already in the stack (or have already been processed...)
          ObjectId objAlreadyOtherSide = bpOtherSide->getPolyhedronWherePolygonIs();
          if(objAlreadyOtherSide!=objectOtherPolygon) {
            cerr << "Processing triangle from meshid: " << tri.first->getMeshId() << endl;
            cerr << "Objects bounded by this edge: " << objectsBoundedByThisEdge.first << " " << objectsBoundedByThisEdge.second << endl;
            cerr << "Already other side, should be, this side: " << objAlreadyOtherSide << " " << objectOtherPolygon << " " << objectThisMesh << endl << endl;
          }
          assert(bpOtherSide->getPolyhedronWherePolygonIs()==objectOtherPolygon);
        }
        
      }
    
  }

  //cerr << "Checking if everything ok... " << endl;
  int ctDontKnow = 0;
  for(BoundaryPolygon&bp:tri.second) {
    if(bp.getPolyhedronWherePolygonIs()==DONT_KNOW_ID || bp.getPolyhedronWherePolygonIs()==DONT_KNOW_FLAG) { //do we know the location of this polygon?
      ctDontKnow++;
    }    
  }
  assert(ctDontKnow==0); //the DFS should find all polygons and label them...

}

void locateTrianglesAndPolygonsInOtherMeshOriginal(const Nested3DGridWrapper *uniformGrid, 
                                            MeshIntersectionGeometry &geometry, 
                                            const unordered_set<const InputTriangle *> trianglesThatIntersect[2],
                                            vector< pair<const InputTriangle *,vector<BoundaryPolygon>> > polygonsFromRetesselationOfEachTriangle[2],
                                            int meshId,
                                            vector<ObjectId> &locationOfEachNonIntersectingTrianglesInOtherMesh) {  
    
    vector<InputTriangle> *inputTriangles = geometry.inputTriangles;

    timespec t0,t1,t0Function;

    
    clock_gettime(CLOCK_REALTIME, &t0);
    t0Function  = t0;

    

    clock_gettime(CLOCK_REALTIME, &t0);
    
    int numInputVerticesCoordinatesThisMesh = geometry.getNumVertices(meshId);

    vector<ObjectId> locationEachVertex(numInputVerticesCoordinatesThisMesh,DONT_KNOW_FLAG);
    vector<int> connectedComponentEachVertex(numInputVerticesCoordinatesThisMesh,DONT_KNOW_FLAG);
    vector<int> sampleVertexIdFromEachConnectedComponent;
    int numComponents;

{
    

    vector<vector<int> > adjList(numInputVerticesCoordinatesThisMesh);
    {
      cerr << "Num of vertices in adj list: " << numInputVerticesCoordinatesThisMesh << endl;
      Timer t; cerr << "Add data to adj. list " ; 
      for(const InputTriangle&t:inputTriangles[meshId]) {
        if(trianglesThatIntersect[meshId].count(&t)==0) { //this triangle does not intersect the other mesh...
          adjList[t.getInputVertex(0)->getId()].push_back(t.getInputVertex(1)->getId());
          adjList[t.getInputVertex(0)->getId()].push_back(t.getInputVertex(2)->getId()); 

          adjList[t.getInputVertex(1)->getId()].push_back(t.getInputVertex(0)->getId());
          adjList[t.getInputVertex(1)->getId()].push_back(t.getInputVertex(2)->getId());

          adjList[t.getInputVertex(2)->getId()].push_back(t.getInputVertex(0)->getId());
          adjList[t.getInputVertex(2)->getId()].push_back(t.getInputVertex(1)->getId());        
        } 
      }
    }

    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to create adj. list: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    clock_gettime(CLOCK_REALTIME, &t0);

    

    cerr << "Labeling connected components\n";
    
    numComponents = labelConnectedComponentsEachVertex(adjList,connectedComponentEachVertex,sampleVertexIdFromEachConnectedComponent);
    assert(numComponents==sampleVertexIdFromEachConnectedComponent.size());

    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to compute CCs: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    clock_gettime(CLOCK_REALTIME, &t0);
}




    cerr << "Num connected components to locate: " << numComponents << "\n";

    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to free adj. list memory: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;

    clock_gettime(CLOCK_REALTIME, &t0);


    vector<InputVertex> verticesToLocateInOtherMesh;
    verticesToLocateInOtherMesh.reserve(numComponents);

    for(int i=0;i<numComponents;i++) {      
      int vertexToLocateId = sampleVertexIdFromEachConnectedComponent[i];

      verticesToLocateInOtherMesh.push_back(InputVertex(meshId,vertexToLocateId));           
    }
      
    int posStartVerticesOfIntersectingTrianglesInThisMesh = verticesToLocateInOtherMesh.size();

    int numPolygonsFromRetesselation = 0;
    int numTriFromRetesselation = 0;
    for(const auto &tri:polygonsFromRetesselationOfEachTriangle[meshId]) {
      const auto &boundaryPolygons = tri.second;
      numPolygonsFromRetesselation += boundaryPolygons.size();
      for(const BoundaryPolygon &b:boundaryPolygons)
        numTriFromRetesselation += b.triangulatedPolygon.size();
    }

    cerr << "Mesh " << meshId << " Num boundary polygons : " << numPolygonsFromRetesselation << endl;         
    cerr << "Mesh " << meshId << " Num tri from retesselation: " << numTriFromRetesselation << endl;
    

 
    
    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to select vertices to locate: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    clock_gettime(CLOCK_REALTIME, &t0);


    //First let's locate the triangles/polygons that contain at least one non-shared vertex.
    
    
    vector<ObjectId> locationOfEachVertexInOtherMesh(verticesToLocateInOtherMesh.size());
    //vertices of mesh "meshId" will be located in mesh "1-meshId"

    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to compute center and get grid cell of center of intersecting triangles: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;


    //Locate the vertices...    

    PinMesh pointLocationAlgorithm(uniformGrid,&geometry);
    clock_gettime(CLOCK_REALTIME, &t0);
    pointLocationAlgorithm.locateVerticesInObject(verticesToLocateInOtherMesh,locationOfEachVertexInOtherMesh,1-meshId);
    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to locate: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;


    //----------------------------------------------------------------------------------------------------------
    //now, we know in what object of the other mesh each triangle that does not intersect other triangles is...

    clock_gettime(CLOCK_REALTIME, &t0);


    const int numInputTrianglesThisMesh = inputTriangles[meshId].size();

    locationOfEachNonIntersectingTrianglesInOtherMesh = vector<ObjectId>(numInputTrianglesThisMesh,DONT_KNOW_FLAG);

    int ct =0;
    for(int tid = 0; tid < numInputTrianglesThisMesh;tid++) {
      const InputTriangle &t = inputTriangles[meshId][tid];
      if(trianglesThatIntersect[meshId].count(&t)==0) {
        int connectedComponentOfThisVertex = connectedComponentEachVertex[t.getInputVertex(0)->getId()];
        locationOfEachNonIntersectingTrianglesInOtherMesh[tid] = locationOfEachVertexInOtherMesh[connectedComponentOfThisVertex];
      }
    }

    //next step: locate triangles intersecting other triangles...

    int ctOnlySharedVertices = 0;
    //locationOfPolygonsFromRetesselationInOtherMesh = vector<ObjectId>(numPolygonsFromRetesselation,DONT_KNOW_FLAG);


    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total copy triangle labels: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    clock_gettime(CLOCK_REALTIME, &t0);

    const int numPolygonsFromRetesselationToProcess = polygonsFromRetesselationOfEachTriangle[meshId].size();
    #pragma omp parallel for
    for(int i =0;i<numPolygonsFromRetesselationToProcess;i++) {
      auto &tri= polygonsFromRetesselationOfEachTriangle[meshId][i];
      auto &boundaryPolygons = tri.second;

      bool locatedAllPolygons = true;
      for(BoundaryPolygon &polygon:boundaryPolygons) {
        const Vertex *nonSharedVertex = getNonSharedVertextFromPolygon(&(*polygon.vertexSequence.begin()),&(*polygon.vertexSequence.end()));
        if(nonSharedVertex!=NULL) {
          int connectedComponentOfThisVertex = connectedComponentEachVertex[nonSharedVertex->getId()];
          polygon.setPolyhedronWherePolygonIs(locationOfEachVertexInOtherMesh[connectedComponentOfThisVertex]);          
        } else {
          locatedAllPolygons = false;
          ctOnlySharedVertices++;
        }
      }
      if(!locatedAllPolygons) {
        locatePolygonsOtherMeshUsingDFS(geometry,tri);
      }
    }

    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total locate polygons using DFS: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    
    cerr << "Num polygons with only shared vertices: " << ctOnlySharedVertices << endl;
    cerr << "Num polygons with input vertices: " << numPolygonsFromRetesselation-ctOnlySharedVertices << endl;
     
    


    
    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total entire location functions: " << convertTimeMsecs(diff(t0Function,t1))/1000 << endl;    
}



//locationOfEachNonIntersectingTrianglesInOtherMesh = vector<ObjectId>(numInputTrianglesThisMesh,DONT_KNOW_FLAG);
//locationOfEachNonIntersectingTrianglesInOtherMesh will have one entry for each input triangle
//locationOfEachNonIntersectingTrianglesInOtherMesh[id] will be the location of triangle with this id, if this triangle does not intersect other mesh.
vector<pair<int,int> > raggedArray;
void locateTrianglesAndPolygonsInOtherMesh(const Nested3DGridWrapper *uniformGrid, 
                                            MeshIntersectionGeometry &geometry, 
                                            const unordered_set<const InputTriangle *> trianglesThatIntersectEachMesh[2],
                                            vector< pair<const InputTriangle *,vector<BoundaryPolygon>> > polygonsFromRetesselationOfEachTriangle[2],
                                            int meshId,
                                            vector<ObjectId> &locationOfEachNonIntersectingTrianglesInOtherMesh) {  
    
    vector<InputTriangle> *inputTriangles = geometry.inputTriangles;
    const unordered_set<const InputTriangle *> &trianglesThatIntersect = trianglesThatIntersectEachMesh[meshId];

    timespec t0,t1,t0Function;

    
    clock_gettime(CLOCK_REALTIME, &t0);
    t0Function  = t0;

        
    int numInputVerticesCoordinatesThisMesh = geometry.getNumVertices(meshId);

    vector<int> connectedComponentEachVertex(numInputVerticesCoordinatesThisMesh,DONT_KNOW_FLAG);
    vector<int> sampleVertexIdFromEachConnectedComponent;
    int numComponents;

{
    raggedArray.resize((inputTriangles[meshId].size()*6));
    {
      Timer t(" create ragged array adj. list ");
      
      int elementsRaggedArray = 0;
      int numTriangles = inputTriangles[meshId].size();

      #pragma omp parallel for
      for(int i=0;i<numTriangles;i++) {
        const InputTriangle &t = inputTriangles[meshId][i];
        int a=-1,b=-1,c=-1;

        if(!t.doesIntersectOtherTriangles) {
          a = t.getInputVertex(0)->getId();
          b = t.getInputVertex(1)->getId();
          c = t.getInputVertex(2)->getId();
        }
        { //this triangle does not intersect the other mesh...
          int startPosRagged;

         // #pragma omp atomic capture
          startPosRagged = i*6;//elementsRaggedArray+=6;
         
          //startPosRagged -= 6;
          raggedArray[startPosRagged+0].first = a;
          raggedArray[startPosRagged+0].second = b;

          raggedArray[startPosRagged+1].first = b;
          raggedArray[startPosRagged+1].second = a;

          raggedArray[startPosRagged+2].first = a;
          raggedArray[startPosRagged+2].second = c;

          raggedArray[startPosRagged+3].first = c;
          raggedArray[startPosRagged+3].second = a;

          raggedArray[startPosRagged+4].first = b;
          raggedArray[startPosRagged+4].second = c;

          raggedArray[startPosRagged+5].first = c;
          raggedArray[startPosRagged+5].second = b;
        }
      }

      
    }
    __gnu_parallel::sort(raggedArray.begin(),raggedArray.end());
    vector<pair<int,int> >::iterator it = std::unique (raggedArray.begin(), raggedArray.end());
    raggedArray.resize( std::distance(raggedArray.begin(),it) ); // 10 20 30 20 10 

    vector<pair<int,int> > adjList2(numInputVerticesCoordinatesThisMesh);//

    cerr << "Elements ragged array: " << raggedArray.size() << endl;

    #pragma omp parallel for
    for(int i=0;i<numInputVerticesCoordinatesThisMesh;i++) adjList2[i].first = adjList2[i].second = -1;

    int numElemRaggedArray = raggedArray.size();
    int lastSeen = -1;
    for(int i=0;i<numElemRaggedArray;i++) {
      if(raggedArray[i].first != lastSeen) {
        lastSeen = raggedArray[i].first;
        adjList2[lastSeen].first = i;
        adjList2[lastSeen].second = i+1;
      } else if(lastSeen!=-1) {
        //-1 represents a non initialized position... this vertex doesn't exist
        adjList2[lastSeen].second++;
      }

    }

    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time so far after array...: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;

    


    

    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to create adj. list: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    


    

    
    
    clock_gettime(CLOCK_REALTIME, &t0);
    cerr << "Labeling connected components\n";
    
    numComponents = labelConnectedComponentsEachVertex(adjList2,raggedArray,connectedComponentEachVertex,sampleVertexIdFromEachConnectedComponent);
    
    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to compute CCs: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    clock_gettime(CLOCK_REALTIME, &t0);

    
    //#define DEBUGGING_MODE
    #ifdef DEBUGGING_MODE
    {
      vector<vector<int> > adjList(numInputVerticesCoordinatesThisMesh);
      cerr << "Num of vertices in adj list: " << numInputVerticesCoordinatesThisMesh << endl;
      Timer t; cerr << "Add data to adj. list " ; 
      for(const InputTriangle&t:inputTriangles[meshId]) {
        if(trianglesThatIntersect.count(&t)==0) { //this triangle does not intersect the other mesh...
          adjList[t.getInputVertex(0)->getId()].push_back(t.getInputVertex(1)->getId());
          adjList[t.getInputVertex(0)->getId()].push_back(t.getInputVertex(2)->getId()); 

          adjList[t.getInputVertex(1)->getId()].push_back(t.getInputVertex(0)->getId());
          adjList[t.getInputVertex(1)->getId()].push_back(t.getInputVertex(2)->getId());

          adjList[t.getInputVertex(2)->getId()].push_back(t.getInputVertex(0)->getId());
          adjList[t.getInputVertex(2)->getId()].push_back(t.getInputVertex(1)->getId());        
        } 
      }

      for(int i=0;i<adjList.size();i++) {
        set<int> elementsInAdj1(adjList[i].begin(),adjList[i].end());
        set<int> elementsInAdj2;
        //for(int j:elementsInAdj1) cerr << j << " " ; cerr << endl;
        for(int j=adjList2[i].first;j<adjList2[i].second;j++) {
          //cerr << raggedArray[j].second << " ";
          elementsInAdj2.insert(raggedArray[j].second);
        } //cerr << endl;

        assert(elementsInAdj2==elementsInAdj1);
      }


      vector<int> connectedComponentEachVertex2(numInputVerticesCoordinatesThisMesh,DONT_KNOW_FLAG);
      vector<int> sampleVertexIdFromEachConnectedComponent2;
      int nc2 =   labelConnectedComponentsEachVertex(adjList,connectedComponentEachVertex2,sampleVertexIdFromEachConnectedComponent2);
      cerr << numComponents << " " << nc2 << endl;
      assert(nc2==numComponents);
      //assert(connectedComponentEachVertex==connectedComponentEachVertex2);
    }
    #endif

    assert(numComponents==sampleVertexIdFromEachConnectedComponent.size());

    
}
    //exit(0);



    cerr << "Num connected components to locate: " << numComponents << "\n";

    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to free adj. list memory: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;

    clock_gettime(CLOCK_REALTIME, &t0);


    vector<InputVertex> verticesToLocateInOtherMesh;
    verticesToLocateInOtherMesh.reserve(numComponents);

    for(int i=0;i<numComponents;i++) {      
      int vertexToLocateId = sampleVertexIdFromEachConnectedComponent[i];

      verticesToLocateInOtherMesh.push_back(InputVertex(meshId,vertexToLocateId));           
    }
      
    int posStartVerticesOfIntersectingTrianglesInThisMesh = verticesToLocateInOtherMesh.size();

    int numPolygonsFromRetesselation = 0;
    int numTriFromRetesselation = 0;
    for(const auto &tri:polygonsFromRetesselationOfEachTriangle[meshId]) {
      const auto &boundaryPolygons = tri.second;
      numPolygonsFromRetesselation += boundaryPolygons.size();
      for(const BoundaryPolygon &b:boundaryPolygons)
        numTriFromRetesselation += b.triangulatedPolygon.size();
    }

    cerr << "Mesh " << meshId << " Num boundary polygons : " << numPolygonsFromRetesselation << endl;         
    cerr << "Mesh " << meshId << " Num tri from retesselation: " << numTriFromRetesselation << endl;
    

 
    
    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to select vertices to locate: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    clock_gettime(CLOCK_REALTIME, &t0);


    //First let's locate the triangles/polygons that contain at least one non-shared vertex.
    
    
    vector<ObjectId> locationOfEachVertexInOtherMesh(verticesToLocateInOtherMesh.size());
    //vertices of mesh "meshId" will be located in mesh "1-meshId"

    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to compute center and get grid cell of center of intersecting triangles: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;


    //Locate the vertices...    

    PinMesh pointLocationAlgorithm(uniformGrid,&geometry);
    clock_gettime(CLOCK_REALTIME, &t0);
    pointLocationAlgorithm.locateVerticesInObject(verticesToLocateInOtherMesh,locationOfEachVertexInOtherMesh,1-meshId);
    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total time to locate: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;


    //----------------------------------------------------------------------------------------------------------
    //now, we know in what object of the other mesh each triangle that does not intersect other triangles is...

    clock_gettime(CLOCK_REALTIME, &t0);


    const int numInputTrianglesThisMesh = inputTriangles[meshId].size();

    locationOfEachNonIntersectingTrianglesInOtherMesh = vector<ObjectId>(numInputTrianglesThisMesh,DONT_KNOW_FLAG);

    int ct =0;
    #pragma omp parallel for
    for(int tid = 0; tid < numInputTrianglesThisMesh;tid++) {
      const InputTriangle &t = inputTriangles[meshId][tid];
      if(!t.doesIntersectOtherTriangles) {
        int connectedComponentOfThisVertex = connectedComponentEachVertex[t.getInputVertex(0)->getId()];
        locationOfEachNonIntersectingTrianglesInOtherMesh[tid] = locationOfEachVertexInOtherMesh[connectedComponentOfThisVertex];
      }
    }

    //next step: locate triangles intersecting other triangles...

    int ctOnlySharedVertices = 0;
    //locationOfPolygonsFromRetesselationInOtherMesh = vector<ObjectId>(numPolygonsFromRetesselation,DONT_KNOW_FLAG);


    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total copy triangle labels: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    clock_gettime(CLOCK_REALTIME, &t0);

    const int numPolygonsFromRetesselationToProcess = polygonsFromRetesselationOfEachTriangle[meshId].size();
    #pragma omp parallel for
    for(int i =0;i<numPolygonsFromRetesselationToProcess;i++) {
      auto &tri= polygonsFromRetesselationOfEachTriangle[meshId][i];
      auto &boundaryPolygons = tri.second;

      bool locatedAllPolygons = true;
      for(BoundaryPolygon &polygon:boundaryPolygons) {
        const Vertex *nonSharedVertex = getNonSharedVertextFromPolygon(&(*polygon.vertexSequence.begin()),&(*polygon.vertexSequence.end()));
        if(nonSharedVertex!=NULL) {
          int connectedComponentOfThisVertex = connectedComponentEachVertex[nonSharedVertex->getId()];
          polygon.setPolyhedronWherePolygonIs(locationOfEachVertexInOtherMesh[connectedComponentOfThisVertex]);          
        } else {
          locatedAllPolygons = false;
          ctOnlySharedVertices++;
        }
      }
      if(!locatedAllPolygons) {
        locatePolygonsOtherMeshUsingDFS(geometry,tri);
      }
    }

    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total locate polygons using DFS: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    
    cerr << "Num polygons with only shared vertices: " << ctOnlySharedVertices << endl;
    cerr << "Num polygons with input vertices: " << numPolygonsFromRetesselation-ctOnlySharedVertices << endl;
     
    


    
    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "Total entire location functions: " << convertTimeMsecs(diff(t0Function,t1))/1000 << endl;    
}


//----------------------------------------------------------------------------
// TODO: not all vertices from the retesselated triangles will be in the output (?)... we do not need to store them!
//Each vector represents the vertices of a layer

//Each vector represents a set of objects in the same layer
//The objects are represented by a set of triangles (defining their boundaries)

double classifyTrianglesAndGenerateOutput(const Nested3DGridWrapper *uniformGrid, 
                                        MeshIntersectionGeometry &geometry, 
                                        const unordered_set<const InputTriangle *> trianglesThatIntersect[2],
                                        vector< pair<const InputTriangle *,vector<BoundaryPolygon>> > polygonsFromRetesselationOfEachTriangle[2],                                                                             
                                        ostream &outputStream) {
	timespec t0,t0ThisFunction,t1;
	clock_gettime(CLOCK_REALTIME, &t0);
	t0ThisFunction = t0;

	//int ctIntersectingTrianglesTotal =0;
  vector<InputTriangle> *inputTriangles = geometry.inputTriangles;

	vector<InputTriangle> outputTriangles[2]; //output triangles generated from non retesselated input triangles
	vector<RetesselationTriangle> outputTrianglesFromRetesselation[2];
	for(int meshId=0;meshId<2;meshId++){
    timespec t0,t1;

    vector<ObjectId> locationOfEachNonIntersectingTrianglesInOtherMesh;

    clock_gettime(CLOCK_REALTIME, &t0);

    locateTrianglesAndPolygonsInOtherMesh(uniformGrid,geometry,trianglesThatIntersect,polygonsFromRetesselationOfEachTriangle,meshId,locationOfEachNonIntersectingTrianglesInOtherMesh);
     //TODO: remove this....
    

    clock_gettime(CLOCK_REALTIME, &t1);

    cerr << "# Time to locate vertices in other mesh: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    clock_gettime(CLOCK_REALTIME, &t0);

		const int numTrianglesThisMesh = inputTriangles[meshId].size();
		int ctTrianglesProcessed = 0;


    vector<InputTriangle> &outputTrianglesThisMesh = outputTriangles[meshId];

    for(int i=0;i<numTrianglesThisMesh;i++) {
        const InputTriangle &t=inputTriangles[meshId][i];
        if(!t.doesIntersectOtherTriangles) {//if(trianglesThatIntersect[meshId].count(&t)==0) { //this triangle does not intersect the other mesh...
          //this will (probably) be an output triangle...
          ObjectId objWhereTriangleIs = locationOfEachNonIntersectingTrianglesInOtherMesh[i];//locationOfEachVertexInOtherMesh[ctTrianglesProcessed++];   
          //cerr << "obj: " << objWhereTriangleIs << endl;    
          if (objWhereTriangleIs!=OUTSIDE_OBJECT) {
            //if the triangle is not outside the other mesh, it will be in the output (we still need to update the left/right objects correctly...)
            outputTrianglesThisMesh.push_back(t);
            
          }        
        }     
    }

    /*vector<InputTriangle> &outputTrianglesThisMesh = outputTriangles[meshId];
    int ctOutputTriangles = 0;
    #pragma omp parallel
    {
      int myCtOutputTriangles = 0;

      #pragma omp for
      for(int i=0;i<numTrianglesThisMesh;i++) {
        const InputTriangle &t=inputTriangles[meshId][i];
        if(!t.doesIntersectOtherTriangles) {//if(trianglesThatIntersect[meshId].count(&t)==0) { //this triangle does not intersect the other mesh...
          //this will (probably) be an output triangle...
          ObjectId objWhereTriangleIs = locationOfEachNonIntersectingTrianglesInOtherMesh[i];//locationOfEachVertexInOtherMesh[ctTrianglesProcessed++];   
          //cerr << "obj: " << objWhereTriangleIs << endl;    
          if (objWhereTriangleIs!=OUTSIDE_OBJECT) {
            //if the triangle is not outside the other mesh, it will be in the output (we still need to update the left/right objects correctly...)
            //outputTriangles[meshId].push_back(t);
            myCtOutputTriangles++;
          }        
        }     
      }
      
      #pragma omp critical
      {
        ctOutputTriangles += myCtOutputTriangles;
      }
    }
		outputTrianglesThisMesh.resize(ctOutputTriangles);

    int ctOutputTrianglesInserted = 0;
    #pragma omp parallel for
      for(int i=0;i<numTrianglesThisMesh;i++) {
        const InputTriangle &t=inputTriangles[meshId][i];
        if(!t.doesIntersectOtherTriangles) {//if(trianglesThatIntersect[meshId].count(&t)==0) { //this triangle does not intersect the other mesh...
          //this will (probably) be an output triangle...
          ObjectId objWhereTriangleIs = locationOfEachNonIntersectingTrianglesInOtherMesh[i];//locationOfEachVertexInOtherMesh[ctTrianglesProcessed++];   
          //cerr << "obj: " << objWhereTriangleIs << endl;    
          if (objWhereTriangleIs!=OUTSIDE_OBJECT) {
            //if the triangle is not outside the other mesh, it will be in the output (we still need to update the left/right objects correctly...)
            //outputTriangles[meshId].push_back(t);

            int posInsert = __sync_fetch_and_add(&ctOutputTrianglesInserted,1) ;
            outputTrianglesThisMesh[posInsert] = t;
          }        
        }     
      }*/


   
    //for each polygon   
    bool hadTriangleDontKnow = false;
    int numTrianglesRetesselated = polygonsFromRetesselationOfEachTriangle[meshId].size();    
    for(int i=0;i<numTrianglesRetesselated;i++) {
      //for each boundary polygon from this triangle...
      for(const BoundaryPolygon&p:polygonsFromRetesselationOfEachTriangle[meshId][i].second) {
        ObjectId objWhereTriangleIs = p.getPolyhedronWherePolygonIs(); //locationOfTrianglesFromRetesselationInTheOtherMesh[i];//locationOfEachVertexInOtherMesh[ctTrianglesProcessed++];          
        
        if (objWhereTriangleIs!=OUTSIDE_OBJECT) {   
          for(array<const Vertex *,3> tris:p.triangulatedPolygon) {
            assert(tris[0]->getMeshId()<3);
            outputTrianglesFromRetesselation[meshId].push_back( RetesselationTriangle(*tris[0], *tris[1], *tris[2],p.above, p.below));
          }
        } 
        if(objWhereTriangleIs==DONT_KNOW_FLAG || objWhereTriangleIs==DONT_KNOW_ID) {
          hadTriangleDontKnow = true;
        }   
      }           
    }

    assert(!hadTriangleDontKnow); //this shouldn't happen...


    clock_gettime(CLOCK_REALTIME, &t1);
    cerr << "# Time to classify triangles vertices in other mesh: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
    clock_gettime(CLOCK_REALTIME, &t0);
	}
	clock_gettime(CLOCK_REALTIME, &t1);
  cerr << "Time to locate vertices and classify triangles: " << convertTimeMsecs(diff(t0ThisFunction,t1))/1000 << endl;
  
  clock_gettime(CLOCK_REALTIME, &t0); 


  #ifdef DEBUGGING_MODE
  for(int meshId=0;meshId<2;meshId++){
    const int szOutputTrianglesFromIntersection = outputTrianglesFromRetesselation[meshId].size();
    for(int i=0;i<szOutputTrianglesFromIntersection;i++) {
      const RetesselationTriangle &t = outputTrianglesFromRetesselation[meshId][i]; 
     // cerr << t.getVertex(0)->getMeshId() << " " << t.getVertex(1)->getMeshId() << endl;
      assert(t.getVertex(0)->getMeshId()<3);
    }  
  }
  #endif

  //Currently, let's write all vertices to the output
	int numVerticesInEachMesh[3] = {geometry.getNumVertices(0),geometry.getNumVertices(1),geometry.getNumVertices(2)};

  int baseIdOfVertices[3];
  baseIdOfVertices[0] = 0; //vertices originally from the first mesh will be counted starting on 1
  baseIdOfVertices[1] = baseIdOfVertices[0] + numVerticesInEachMesh[0]; 
  baseIdOfVertices[2] = baseIdOfVertices[1] + numVerticesInEachMesh[1]; 

	//compute the new ids of the shared vertices...



  int totalNumberOutputVertices = numVerticesInEachMesh[0] + numVerticesInEachMesh[1] + numVerticesInEachMesh[2];
  int totalNumberOutputVerticesFromNonIntersectingTriangles = numVerticesInEachMesh[0] + numVerticesInEachMesh[1] ;
  int totalNumberOutputTriangles = outputTriangles[0].size() + outputTriangles[1].size() + outputTrianglesFromRetesselation[0].size() + outputTrianglesFromRetesselation[1].size();

  
  unordered_map<pair<int,int>,int> edgesIds; //maybe use unordered_map for performance (if necessary...)
  vector<pair<int,int> > outputEdges;
 
	//vector<pair<int,int> > outputEdgesWithRepetition[2];

	for(int meshId=0;meshId<2;meshId++) {
		int numNewEdgesToAdd =0;
		#pragma omp parallel
		{			
			
			vector<pair<int,int> > myEdgesFound;	
			
      
      const int sz =  outputTriangles[meshId].size();
			#pragma omp for
		  for(int i=0;i<sz;i++) {
		  	const Triangle &t = outputTriangles[meshId][i];
        int a = t.getVertex(0)->getId() + baseIdOfVertices[meshId];
        int b = t.getVertex(1)->getId() + baseIdOfVertices[meshId];
        int c = t.getVertex(2)->getId() + baseIdOfVertices[meshId];

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
		  	const RetesselationTriangle &t = outputTrianglesFromRetesselation[meshId][i];
				

        int a = t.getVertex(0)->getId() + baseIdOfVertices[t.getVertex(0)->getMeshId()];
        int b = t.getVertex(1)->getId() + baseIdOfVertices[t.getVertex(1)->getMeshId()];
        int c = t.getVertex(2)->getId() + baseIdOfVertices[t.getVertex(2)->getMeshId()];


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
	__gnu_parallel::sort(outputEdges.begin(),outputEdges.end());
	auto newEndItr = unique(outputEdges.begin(),outputEdges.end());
	outputEdges.resize(newEndItr- outputEdges.begin());

	clock_gettime(CLOCK_REALTIME, &t1);
	cerr << "T so far: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;

	
  //This is not necessary for OFF files...
	/*int totalNumberOutputEdges = outputEdges.size();
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
	}*/

	//clock_gettime(CLOCK_REALTIME, &t1);
	//timeClassifyTriangles = convertTimeMsecs(diff(t0ThisFunction,t1))/1000;

  //cerr << "Time to create edges: " << convertTimeMsecs(diff(t0,t1))/1000 << endl;
 // cerr << "Total time (excluding I/O) so far: " << convertTimeMsecs(diff(t0AfterDatasetRead,t1))/1000 << endl;
  clock_gettime(CLOCK_REALTIME, &t0); 

  
  double timeWithoutIO =  convertTimeMsecs(diff(t0ThisFunction,t1))/1000;
  cerr << "Total time before writing output: " << timeWithoutIO << endl;
 


  geometry.reverseRotationIfNecessary(); //if the meshes have been rotated and we are supposed to reverse the rotation...



  //now, let's write everything in the output!
  outputStream << "OFF\n";
  //outputStream << totalNumberOutputVertices << " " << totalNumberOutputEdges << " " << totalNumberOutputTriangles << '\n';
  outputStream << totalNumberOutputVertices << " " <<  totalNumberOutputTriangles << " 0\n";

  //print the coordinates of the vertices...
  
  geometry.storeAllVertices(outputStream);

	//print edges...
	//for(const pair<int,int> &p:outputEdges) {
	//	outputStream << p.first+1 << " " << p.second+1 << "\n"; //in a GTS file we start counting from 1...
	//}

	//print triangles...
	for(int meshId=0;meshId<2;meshId++) 
		for(Triangle &t : outputTriangles[meshId]) {
      int a = t.getVertex(0)->getId() + baseIdOfVertices[meshId];
      int b = t.getVertex(1)->getId() + baseIdOfVertices[meshId];
      int c = t.getVertex(2)->getId() + baseIdOfVertices[meshId];


			if(t.above != OUTSIDE_OBJECT) {
				//according to the right hand rule, the ordering of the vertices should be (c,b,a)
				swap(a,c);
			} 
      outputStream << "3 " << a << " " << b << " " << c << "\n";
      /*
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
			outputStream << mapEdgesIds2[e.first][e.second]+1 << "\n";*/
		}

    
		for(int meshId=0;meshId<2;meshId++) 
			for(RetesselationTriangle &t : outputTrianglesFromRetesselation[meshId]) {
				int a = t.getVertex(0)->getId() + baseIdOfVertices[t.getVertex(0)->getMeshId()];
				int b = t.getVertex(1)->getId() + baseIdOfVertices[t.getVertex(1)->getMeshId()];
				int c = t.getVertex(2)->getId() + baseIdOfVertices[t.getVertex(2)->getMeshId()];

				

				if(t.above != OUTSIDE_OBJECT) {
					//according to the right hand rule, the ordering of the vertices should be (c,b,a)
					swap(a,c);
				} 

        outputStream << "3 " << a << " " << b << " " << c << "\n";

        /*
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
				outputStream << mapEdgesIds2[e.first][e.second]+1 << "\n";*/
			}
    
	
		cerr << "Output vertices         : " << totalNumberOutputVertices << endl;
		//cerr << "Output edges            : " << totalNumberOutputEdges << endl;
		cerr << "Output triangles non int: " << totalNumberOutputTriangles << endl;
		//cerr << "Intersecting triangles  : " << ctIntersectingTrianglesTotal << endl;


    return timeWithoutIO;

}