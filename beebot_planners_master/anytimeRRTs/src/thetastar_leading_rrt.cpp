#include <srl_global_planner/thetastar_leading_rrt.h>
#include <costmap_2d/cost_values.h>
using namespace costmap_2d;

#define DEBUG_LISTS 0
#define DEBUG_LIST_LENGTHS_ONLY 0




/// Initialize static Grid member
Grid* Grid_planner::grid_ = NULL;



Grid_planner::Grid_planner(const ros::NodeHandle& node, base_local_planner::CostmapModel* world_model,
 std::vector<geometry_msgs::Point> footprint_spec, costmap_2d::Costmap2DROS* costmap_ros): nh_(node), world_model_(world_model_), footprint_spec_(footprint_spec), costmap_ros_(costmap_ros) {


    trajectory_ = new Trajectory();

    gridlistener_ = new tf::TransformListener();

    run_cnt_=0;

    map_reading_cnt_=0;

    min_cost_p_ = 0.1;

    THETASTARON_=1;

    Fs_gridplanner_ = 0;

    MAX_LIMIT_SEARCH_STEPS_ = 10000;

    PUBLISH_GRID = 1;


}


/// ==================================================================================
/// initialize()
/// Method to initialize all the ROS related topics
/// ==================================================================================
bool Grid_planner::initialize(){


    /// need to properly initialize goal and start

    pub_discrete_path_ = nh_.advertise<nav_msgs::Path>("/move_base_node/Srl_global_planner/global_path", 1); // to publish the path that will be used as support in RRT

    pub_discrete_path_marker_ = nh_.advertise<visualization_msgs::Marker>("/move_base_node/Srl_global_planner/global_path_marker_", 1); // to publish the path that will be used as support in RRT

    pub_grid_ = nh_.advertise<visualization_msgs::Marker>("/move_base_node/Srl_global_planner/grid_markers", 1); //

    sub_obstacles_ = nh_.subscribe("/move_base_node/global_costmap/costmap",1, &Grid_planner::callbackreadingObstacles,this);

    /// define grid parameters
    nh_.getParam("/move_base_node/minx", minx_);

    nh_.getParam("/move_base_node/miny", miny_);

    nh_.getParam("/move_base_node/grid_width", grid_width_);

    nh_.getParam("/move_base_node/grid_height", grid_height_);

    /// cell sizes
    nh_.getParam("/move_base_node/cell_width", cellwidth_);

    nh_.getParam("/move_base_node/cell_height", cellheight_);

    nh_.getParam("/move_base_node/select_globalplanner", THETASTARON_);

    nh_.getParam("/move_base_node/grid_planner_f", Fs_gridplanner_);

    nh_.getParam("/move_base_node/DEB_RRT", DEB_INFO_);

    nh_.getParam("/move_base_node/LEVEL_OBSTACLE_", LEVEL_OBSTACLE_);

    nh_.getParam("/move_base_node/min_cost_p", min_cost_p_);

    nh_.getParam("/move_base_node/MAX_LIMIT_SEARCH_STEPS", MAX_LIMIT_SEARCH_STEPS_);

    nh_.getParam("/move_base_node/planner_frame",planner_frame_);

    nh_.getParam("move_base_node/PUBLISH_GRID", PUBLISH_GRID);


    if(Fs_gridplanner_!=0)
        grid_planner_loop_rate_ = new ros::Rate(Fs_gridplanner_);
    else
        grid_planner_loop_rate_ = new ros::Rate(10);


    costmap_ = costmap_ros_->getCostmap();

    /// TODO: check correctness of the frame of this origin
    minx_ = costmap_->getOriginX();

    miny_ = costmap_->getOriginY();

    grid_width_ = costmap_->getSizeInMetersX();

    grid_height_ = costmap_->getSizeInMetersY();

    int n_cellx = (int) (costmap_->getSizeInCellsX());

    int n_celly = (int) (costmap_->getSizeInCellsY());

    cellwidth_ = ceilf((grid_width_/n_cellx)*100)/100;

    cellheight_ = ceilf((grid_height_/n_celly)*100)/100;

    ROS_INFO("Global Planner --> Loaded Grid parameters (%f,%f,%f,%f,%f,%f)", minx_, miny_, grid_width_, grid_height_, cellwidth_, cellheight_ );




}



/// ==================================================================================
/// publishGlobalPath(std::vector<GNode>& path, bool replan)
/// Public the Global Path generated by Theta*
/// ==================================================================================
void Grid_planner::publishGlobalPath(std::vector<GNode>& path, bool replan){

    ROS_INFO("Grid Planner --> Trajectory reset");
    trajectory_->reset();
    ROS_INFO("Grid Planner --> Trajectory  done");

    ROS_INFO("Grid Planner --> Path size %d",(int)path.size());
    int i;
    if (replan){


        i = path.size()-2;
    }
    else{

        i = path.size()-1;
    }

    ROS_INFO("Grid Planner --> Start cell generated");
    Tpoint cell=getEnclosingCell(start_.x,start_.y);

    // trajectory_->addPointEnd(Tpoint(cell.x*cellwidth_+minx_, cell.y*cellheight_+miny_, 0));
    trajectory_->addPointEnd(Tpoint(start_.x, start_.y, 0));



    for( ; i >= 0; i--)
    {
        GNode node = path[i];
        Tpoint cell=getEnclosingCell(node.x,node.y);
        // trajectory_->addPointEnd(Tpoint(cell.x*cellwidth_, cell.y*cellheight_, 0));

        trajectory_->addPointEnd(Tpoint(node.x, node.y, 0));

    }






    nav_msgs::Path globalpath;
    ROS_INFO("Grid Planner --> Publishing a path");

    std::vector<Tpoint> path_theta = trajectory_->getPath();
    ROS_INFO("Grid Planner --> Path Size :%d",(int)path.size());

    globalpath.header.frame_id = planner_frame_;
    globalpath.header.stamp = ros::Time();
    globalpath.poses.resize((int)path_theta.size());



    visualization_msgs::Marker path_marker_;
    path_marker_.header.frame_id = planner_frame_;
    path_marker_.header.stamp = ros::Time();
    path_marker_.ns = "grid_planner";
    path_marker_.id = 1;

    path_marker_.type = visualization_msgs::Marker::POINTS;
    path_marker_.color.a = 1;
    path_marker_.color.r = 1.0;
    path_marker_.color.g = 0.0;
    path_marker_.color.b = 0.0;

    path_marker_.scale.x = 0.25;
    path_marker_.scale.y = 0.25;
    path_marker_.scale.z = 0.25;


    path_marker_.action = 0;  // add or modify


    double old_x=0;
    double old_y=0;
    double t=0;

    for (size_t i = 0; i < path_theta.size(); i++) {


        geometry_msgs::PoseStamped posei;

        globalpath.poses[i].header.stamp = ros::Time();
        globalpath.poses[i].header.frame_id= planner_frame_;


        globalpath.poses[i].pose.position.x = path_theta[i].x;
        globalpath.poses[i].pose.position.y = path_theta[i].y;
        globalpath.poses[i].pose.position.z = 1.2;


        globalpath.poses[i].pose.orientation = tf::createQuaternionMsgFromRollPitchYaw(0, 0, path_theta[i].z);




        ///  Workaround to display markers along the Theta* path's segments
        if(i==0){


        geometry_msgs::Point p;
        p.x = path_theta[i].x;
        p.y = path_theta[i].y;
        p.z = 1.2;
        path_marker_.points.push_back(p);
        old_x=path_theta[i].x;
        old_y=path_theta[i].y;


        }
        else{


        while(t<1){

            geometry_msgs::Point p;
            p.x = (path_theta[i].x-old_x)*t+old_x;
            p.y = (path_theta[i].y-old_y)*t+old_y;
            p.z = 1.2;
            path_marker_.points.push_back(p);
            t=t+0.01;

        }


        geometry_msgs::Point p;
        p.x = path_theta[i].x;
        p.y = path_theta[i].y;
        p.z = 1.2;
        path_marker_.points.push_back(p);
        old_x=path_theta[i].x;
        old_y=path_theta[i].y;
        t=0;

        }
        // ROS_INFO("Quaternion: (%f,%f,%f,%f)",orientation.getQx(),orientation.getQy(),orientation.getQz(),orientation.getQw());


    }

    if(path_theta.size()>0){


        /// Should clean also the local planner trajectory
        pub_discrete_path_.publish(globalpath);
        pub_discrete_path_marker_.publish(path_marker_);
        ROS_INFO("Global Planner --> Path Published");

    }


}



/// ============================================================================================
/// callbackreadingObstacles(const nav_msgs::GridCells::ConstPtr& msg)
/// Callback to read all the static obstacles in the scene. Each obstacle point is define as the
/// center of the corresponding cell.
/// ============================================================================================

void Grid_planner::callbackreadingObstacles(const nav_msgs::OccupancyGrid::ConstPtr& msg){

    /// Read the obstacles in the messagge and save them in the vector obstacle_positions
    visualization_msgs::Marker grid_marker;


    grid_marker.header.frame_id = planner_frame_;
    grid_marker.header.stamp = ros::Time();
    grid_marker.ns = "grid_planner";
    grid_marker.id = 1111111;

    grid_marker.type = visualization_msgs::Marker::POINTS;
    grid_marker.color.a = 1;
    grid_marker.color.r = 0.0;
    grid_marker.color.g = 0.0;
    grid_marker.color.b = 1.0;

    grid_marker.scale.x = cellwidth_;
    grid_marker.scale.y = cellheight_;
    grid_marker.scale.z = 0.1;


    grid_marker.action = 0;  // add or modify


    costmap_frame_ = msg->header.frame_id ;

    obstacle_positions_.clear();

   if(DEB_INFO_)
       ROS_INFO("Reading Obstacles positions..");

    std_msgs::Header header = msg->header;
    nav_msgs::MapMetaData info = msg->info;

    int sizemap;
    sizemap = sizeof(msg->data) / sizeof(msg->data[0]);

    if(map_reading_cnt_ == 0 && sizemap>0){

        cellwidth_ = info.resolution;
        cellheight_ =  info.resolution;
        ROS_INFO("Initializing Grid Map.. resolution %f ", info.resolution);

        //  Initialize the grid
        initGrid(minx_,miny_,grid_width_,grid_height_,cellwidth_,cellheight_);
    }



    if(DEB_INFO_)
        ROS_INFO("Got map %d %d in the %s frame", info.width, info.height, costmap_frame_.c_str() );



    int occup_prob,n_occupied;
    n_occupied=0;
    double xo, yo;

    double min_prob = LEVEL_OBSTACLE_;

    if(DEB_INFO_)
        ROS_INFO("LEVEL_OBSTACLE_ accepted by the grid planner %d", LEVEL_OBSTACLE_ );


    double min_cost = 255;
    double max_cost = 0;



    // looking for the min of the cost in the grid..

    for (unsigned int x = 0; x < info.width; x++)
        for (unsigned int y = 0; y < info.height; y++){
        /// acces the data in the map, in row-major order
                    occup_prob=msg->data[x+ info.width * y];

                    if(occup_prob<0){
                        // ROS_WARN("obstacle level lower than 0 : %d", occup_prob);
                        // Setting those to an high value!
                        occup_prob = 255;
                    }


                    getGrid()->setCost((x+.5)*info.resolution+info.origin.position.x, (y+.5)*info.resolution+info.origin.position.y, occup_prob);

                    if(occup_prob < min_cost)
                        min_cost = occup_prob;

                    if(occup_prob > max_cost)
                        max_cost = occup_prob;

       }


    if(DEB_INFO_)
        ROS_INFO("Grid planner, min cost %f, max cost %f", min_cost, max_cost);

    // Associating the 110% of the min_cost as min_prob to have an obstacle
    // min_prob = (double)min_cost + ((double)max_cost-(double)min_cost)*min_cost_p_;

    if(DEB_INFO_)
        ROS_INFO("Grid Planner min_pro to be not accepted as obstacle %f ", min_prob );

    for (unsigned int x = 0; x < info.width; x++)
        for (unsigned int y = 0; y < info.height; y++){
            /// acces the data in the map, in row-major order
                        occup_prob=msg->data[x+ info.width * y];

                        if( occup_prob >= ((int)min_prob) ){

                            xo= (x+0.5)*info.resolution+info.origin.position.x;
                            yo= (y+0.5)*info.resolution+info.origin.position.y;

                            getGrid()->setOccupied(xo,yo);

                            geometry_msgs::Point p;
                            p.x = xo;
                            p.y = yo;
                            p.z = 0.5;

                            grid_marker.points.push_back(p);

                            n_occupied++;

                        }


			               if( occup_prob < 0 ){

	                      xo= (x+0.5)*info.resolution+info.origin.position.x;
                        yo= (y+0.5)*info.resolution+info.origin.position.y;
                        getGrid()->setOccupied(xo,yo);
                        n_occupied++;
			               }
           }


    if(sizemap>0)
        map_reading_cnt_++;

    if(PUBLISH_GRID>0){

          pub_grid_.publish(grid_marker);
          ROS_INFO("Global Planner --> Grid markers published");

      }else{
	ROS_INFO("Global Planner --> Grid markers not publisher, flag %d",PUBLISH_GRID);
	}
}

/// ============================================================================================
/// setGoal(double x,double y, double z)
/// Method to set the Goal
/// ============================================================================================

void Grid_planner::setGoal(double x,double y, double z, std::string goal_frame){

    ROS_INFO("Setting Goal (%f, %f), Goal Frame %s", x, y , goal_frame.c_str());

    tf::StampedTransform transform;

    try{

        // will transform data in the goal_frame into the costmap_frame_
        gridlistener_->waitForTransform( costmap_frame_, goal_frame, ros::Time::now(), ros::Duration(0.20));
        gridlistener_->lookupTransform( costmap_frame_, goal_frame, ros::Time::now(), transform);

    }
    catch(tf::TransformException){

        ROS_ERROR("Failed to transform Gaol Transform in Grid Planner");
        return;
    }

    tf::Pose source;

    tf::Quaternion q= tf::createQuaternionFromRPY(0,0,z);

    tf::Matrix3x3 base(q);

    source.setOrigin(tf::Vector3(x, y, 0));

    source.setBasis(base);

    /// Apply the proper transform
    tf::Pose result = transform*source;

        /// Check if in obstacle
    unsigned int x_c, y_c;
    costmap_->worldToMap(x, y, x_c, y_c);

    unsigned char cost = costmap_->getCost(x_c, y_c);
      //if(cost == LETHAL_OBSTACLE || cost == INSCRIBED_INFLATED_OBSTACLE)
    if(cost == LETHAL_OBSTACLE || cost == INSCRIBED_INFLATED_OBSTACLE || cost == NO_INFORMATION)
                ROS_ERROR("Start in Obstacle cell!! ");

    this->goal_.x= result.getOrigin().x();
    this->goal_.y= result.getOrigin().y();
    this->goal_.z= tf::getYaw( result.getRotation());



}


/// ============================================================================================
/// setGoal(double x,double y, double z)
/// Method to set the start pose
/// ============================================================================================
void Grid_planner::setStart(double x, double y, double theta, std::string start_frame){


    ROS_INFO("Setting Start (%f, %f), Start Frame %s", x, y, start_frame.c_str());

    tf::StampedTransform transform;

    try{

        // will transform data in the start_frame into the costmap_frame_
        gridlistener_->waitForTransform( costmap_frame_, start_frame, ros::Time::now(), ros::Duration(0.20));
        gridlistener_->lookupTransform( costmap_frame_, start_frame, ros::Time::now(), transform);

    }
    catch(tf::TransformException){

        ROS_ERROR("Failed to transform Gaol Transform in Grid Planner");
        return;
    }

    tf::Pose source;

    tf::Quaternion q= tf::createQuaternionFromRPY(0,0,theta);

    tf::Matrix3x3 base(q);

    source.setOrigin(tf::Vector3(x, y, 0));

    source.setBasis(base);

    /// Apply the proper transform
    tf::Pose result = transform*source;

    /// Check if in obstacle
    unsigned int x_c, y_c;
    costmap_->worldToMap(x, y, x_c, y_c);

    unsigned char cost = costmap_->getCost(x_c, y_c);

    //if(cost == LETHAL_OBSTACLE || cost == INSCRIBED_INFLATED_OBSTACLE)
    if(cost == LETHAL_OBSTACLE || cost == INSCRIBED_INFLATED_OBSTACLE || cost == NO_INFORMATION)
                ROS_ERROR("Start in Obstacle cell!! ");


    this->start_.x = x;
    this->start_.y = y;
    this->start_.z = theta;

}





/// ==================================================================================
/// initGrid(double ax, double ay, double aw, double ah,double cwidth, double cheight)
/// Initialize properly the grid
/// ==================================================================================
void Grid_planner::initGrid(double ax, double ay, double aw, double ah,double cwidth, double cheight){


    /// Generate the grid... Or initialize it according to the place where you read the size of the scene
    ROS_INFO("Global Planner --> Initialize grid...");
    Grid_planner::grid_ = new Grid(ax, ay, aw, ah, cwidth, cheight,  (int) (costmap_->getSizeInCellsX()),  (int) (costmap_->getSizeInCellsY()));
    ROS_INFO("Global Planner --> ....  grid Initialized");



}

/// ==================================================================================
/// astar_search
/// Run an A* search
/// ==================================================================================

int Grid_planner::astar_search(std::vector<std::vector<GNode> >& paths, GNode st){



    // prep start and goal nodes
    Tpoint gc = getEnclosingCell(goal_.x, goal_.y);
    GNode gl(gc.x*cellwidth_, gc.y*cellheight_);
    // GNode start(st.x*cellwidth_, st.y*cellheight_);
    // TODO: Need to test it!!!
    GNode start(start_.x,start_.y);

    ROS_INFO("Global Planner --> Start: %d, %d --- Goal: %d, %d ",(int)start.x,(int)start.y,(int)gl.x,(int) gl.y);



    /// STL THETA*

    std::vector<GNode> sol;
    std::vector< std::vector<GNode> > path_sol;
    ThetaStarSearch<GNode> astarsearch( true);

    unsigned int SearchCount = 0;

    const unsigned int NumSearches = 1;

    while(SearchCount < NumSearches)
    {



        // Set Start and goal states

        astarsearch.SetStartAndGoalStates( start, gl );

        unsigned int SearchState;
        unsigned int SearchSteps = 0;

        do
        {
            SearchState = astarsearch.SearchStep();

            SearchSteps++;
#if DEBUG_LISTS

            ROS_INFO("Global Planner --> Steps: %d", (int)SearchSteps);

            int len = 0;

            ROS_INFO("Open:");
            GNode *p = astarsearch.GetOpenListStart();
            while( p )
            {
                len++;
#if !DEBUG_LIST_LENGTHS_ONLY
                ((GNode *)p)->PrintNodeInfo();
#endif
                p = astarsearch.GetOpenListNext();

            }
            ROS_INFO("Global Planner --> Open list has %d",len);

            len = 0;
            ROS_INFO("Global Planner --> Closed");
            p = astarsearch.GetClosedListStart();
            while( p )
            {
                len++;
#if !DEBUG_LIST_LENGTHS_ONLY
                p->PrintNodeInfo();
#endif
                p = astarsearch.GetClosedListNext();
            }
            ROS_INFO("Global Planner --> Closed list has %d nodes",len);
#endif
        }
        while( SearchState == ThetaStarSearch<GNode>::SEARCH_STATE_SEARCHING && SearchSteps < MAX_LIMIT_SEARCH_STEPS_ );

        if(SearchSteps >= MAX_LIMIT_SEARCH_STEPS_){

            ROS_INFO("Grid Planner !! Search did not find a path, MAX_LIMIT_SEARCH_STEPS_ exceeded!");

        }

        if( SearchState == ThetaStarSearch<GNode>::SEARCH_STATE_SUCCEEDED )
        {
            ROS_INFO("Global Planner --> Search found goal state");

            GNode *node = astarsearch.GetSolutionStart();
            int steps = 0;
            int xs,ys;
            node->PrintNodeInfo();
            xs=node->x;
            ys=node->y;
            sol.push_back(GNode(xs,ys));
            ROS_INFO("Global Planner --> Nodes: %d,%d", xs,ys);
            // cout<<" "<<xs<<" "<<ys<<endl;
            for( ;; )
            {
                node = astarsearch.GetSolutionNext();

                if( !node )
                {
                    break;
                }

                node->PrintNodeInfo();

                xs=node->x;
                ys=node->y;
                sol.push_back(GNode(xs,ys));
                ROS_INFO("Node Solution %d, %d",xs,ys);
		steps ++;

            };


            // Once you're done with the solution you can free the nodes up
            astarsearch.FreeSolutionNodes();


        }
        else if( SearchState == ThetaStarSearch<GNode>::SEARCH_STATE_FAILED )
        {
            ROS_INFO("Global Planner --> Search terminated. Did not find goal state");

        }

        // Display the number of loops the search went through
        ROS_INFO("Global Planner --> SearchSteps: %d ",(int)SearchSteps);

        SearchCount ++;

        astarsearch.EnsureMemoryFreed();
    }

    std::reverse(sol.begin(), sol.end());
    paths.push_back(sol);

    int path_size=paths.size();
    ROS_INFO("Global Planner --> Found [ %d ] paths",path_size);

    if (path_size>0){

            return 1;
        }
        else
            return 0;



}


/// ==================================================================================
/// search(std::vector<std::vector<GNode> >& paths, GNode st)
/// Theta* search
/// ==================================================================================
int Grid_planner::thetastar_search(std::vector<std::vector<GNode> >& paths, GNode st){



    // prep start and goal nodes
    Tpoint gc = getEnclosingCell(goal_.x, goal_.y);

    // GNode gl(gc.x*cellwidth_, gc.y*cellheight_);

    GNode gl(goal_.x, goal_.y);

    // GNode start(st.x*cellwidth_, st.y*cellheight_);
    // TODO: Need to test it!!!
    GNode start(start_.x,start_.y);

    ROS_INFO("Global Planner --> Start: %d, %d --- Goal: %d, %d ",(int)start.x,(int)start.y,(int)gl.x,(int) gl.y);



    /// STL THETA*

    std::vector<GNode> sol;
    std::vector< std::vector<GNode> > path_sol;
    ThetaStarSearch<GNode> thetastarsearch( false);

    unsigned int SearchCount = 0;

    const unsigned int NumSearches = 1;

    while(SearchCount < NumSearches)
    {



        // Set Start and goal states

        thetastarsearch.SetStartAndGoalStates( start, gl );

        unsigned int SearchState;
        unsigned int SearchSteps = 0;

        do
        {
            SearchState = thetastarsearch.SearchStep();

            SearchSteps++;
#if DEBUG_LISTS

            ROS_INFO("Global Planner --> Steps: %d", (int)SearchSteps);

            int len = 0;

            ROS_INFO("Open:");
            GNode *p = thetastarsearch.GetOpenListStart();
            while( p )
            {
                len++;
#if !DEBUG_LIST_LENGTHS_ONLY
                ((GNode *)p)->PrintNodeInfo();
#endif
                p = thetastarsearch.GetOpenListNext();

            }
            ROS_INFO("Global Planner --> Open list has %d",len);

            len = 0;
            ROS_INFO("Global Planner --> Closed");
            p = thetastarsearch.GetClosedListStart();
            while( p )
            {
                len++;
#if !DEBUG_LIST_LENGTHS_ONLY
                p->PrintNodeInfo();
#endif
                p = thetastarsearch.GetClosedListNext();
            }
            ROS_INFO("Global Planner --> Closed list has %d nodes",len);
#endif
        }
        while( SearchState == ThetaStarSearch<GNode>::SEARCH_STATE_SEARCHING && SearchSteps < MAX_LIMIT_SEARCH_STEPS_);


        if(SearchSteps >= MAX_LIMIT_SEARCH_STEPS_){

            ROS_INFO("Grid Planner !! Search did not find a path, MAX_LIMIT_SEARCH_STEPS_ exceeded!");

        }

        if( SearchState == ThetaStarSearch<GNode>::SEARCH_STATE_SUCCEEDED )
        {
            ROS_INFO("Global Planner --> Search found goal state");

            GNode *node = thetastarsearch.GetSolutionStart();
            int steps = 0;
            int xs,ys;
            node->PrintNodeInfo();
            xs=node->x;
            ys=node->y;
            sol.push_back(GNode(xs,ys));
            ROS_INFO("Global Planner --> Nodes: %d,%d", xs,ys);
            for( ;; )
            {
                node = thetastarsearch.GetSolutionNext();

                if( !node )
                {
                    break;
                }

                node->PrintNodeInfo();

                xs=node->x;
                ys=node->y;
                sol.push_back(GNode(xs,ys));
                ROS_INFO("Global Planner --> Nodes : %d, %d ",xs, ys);

                steps ++;

            };


            // Once you're done with the solution you can free the nodes up
            thetastarsearch.FreeSolutionNodes();


        }
        else if( SearchState == ThetaStarSearch<GNode>::SEARCH_STATE_FAILED )
        {
            ROS_INFO("Global Planner --> Search terminated. Did not find goal state");

        }

        // Display the number of loops the search went through
        ROS_INFO("Global Planner --> SearchSteps: %d ",(int)SearchSteps);

        SearchCount ++;

        thetastarsearch.EnsureMemoryFreed();
    }

    std::reverse(sol.begin(), sol.end());
    paths.push_back(sol);

    int path_size=paths.size();
    ROS_INFO("Global Planner --> Found [ %d ] paths",path_size);

    if (path_size>0){

        	return 1;
    }
    else
    		return 0;

}


/// ============================================================================================
/// getGrid()
/// This method returns the Grid
/// ============================================================================================
Grid* Grid_planner::getGrid() {

    return Grid_planner::grid_;

}



/// ============================================================================================
/// getEnclosingCell(double x,double y)
/// Method to compute the cell's indeces of the given carteesian point
/// ============================================================================================
Tpoint Grid_planner::getEnclosingCell(double x,double y){


if (((x-Grid_planner::grid_->minx) < 0) || ((y-Grid_planner::grid_->miny) < 0))
        ROS_INFO("Global Planner --> Cell coordinates out of grid! the point is out of the grid");


    unsigned int cellx = (int)((x-Grid_planner::grid_->minx)/cellwidth_);
    unsigned int celly = (int)((y-Grid_planner::grid_->miny)/cellheight_);

    if ((cellx*cellwidth_ > (unsigned int)Grid_planner::grid_->width || (celly*cellheight_ > (unsigned int)Grid_planner::grid_->height)))
        {
            ROS_INFO("Global Planner --> Cell coordinates out of grid! cellx or celly greater");
            ROS_INFO("Global Planner --> cellx: %d, celly: %d ",cellx,celly);
            ROS_INFO("Global Planner considering width and height --> cellx: %d, celly: %d ",cellx*cellwidth_, celly*cellheight_);

        }

    return Tpoint(cellx, celly, 0);

}



/// ============================================================================================
/// run_global_planner()
/// Main loop to run the global planner
/// return 1 if path was found
/// return 0 if no path found
/// ===========================================================================================
int Grid_planner::run_grid_planner(std::vector< geometry_msgs::PoseStamped >& plan){


    run_cnt_++;
    /// ================================
    /// Run
    ///     - read start pose of the robot
    ///     - run the search
    ///     - publish global path
    /// ================================
    std::vector<std::vector<GNode> > global_paths_;
    ROS_INFO("grid Planner --> Generate a new global path");

    ///     - read start pose of the robot
    ROS_INFO("grid Planner --> Robot pose read");
    ROS_INFO("grid Planner --> (Rx,Ry) = (%f,%f)",start_.x,start_.y);
    ROS_INFO("grid Planner --> Start Search");
    ROS_INFO("grid Planner --> Grid Size:%d", (int)getGrid()->cells.size());
    ///     - run the search
    if(THETASTARON_){

        if( !thetastar_search(global_paths_,GNode(start_.x,start_.y)) )
            return 0;
    }else{

        if( !astar_search(global_paths_,GNode(start_.x,start_.y)) )
            return 0;
    }

    ROS_INFO("grid grid --> Search finished");
    ROS_INFO("grid Planner --> Global path size : %d",(int)global_paths_[0].size());
    ///     - publish global path

    publishGlobalPath(global_paths_[0],true);


    std::vector<GNode> path = global_paths_[0];

    ROS_INFO("grid Planner --> Trajectory  done");

    ROS_INFO("grid Planner --> Path size %d",(int)path.size());

    if( ((int)path.size()) == 0)
    {
        ROS_WARN("No Grid Path need to replan!");
        return 0;

    }

    int i;
    i = path.size()-2;


    ROS_INFO("grid Planner --> Start cell generated");
    Tpoint cell=getEnclosingCell(start_.x,start_.y);
    geometry_msgs::PoseStamped start;
    start.header.seq = run_cnt_;
    start.header.stamp = ros::Time::now();
    start.header.frame_id = costmap_frame_;
    start.pose.position.x = cell.x*cellwidth_ + minx_;
    start.pose.position.y = cell.y*cellheight_+ miny_;
    ROS_INFO("Adding the start pose %f %f ", cell.x*cellwidth_ + minx_, cell.y*cellheight_ + miny_);
    plan.push_back(start);

    for( ; i >= 0; i--)
    {
        GNode node = path[i];
        Tpoint cell=getEnclosingCell(node.x,node.y);
      	ROS_INFO("Node %f %f ", node.x, node.y);
      	ROS_INFO("Copying Path, cell  %f %f", cell.x*cellwidth_, cell.y*cellheight_);
      	ROS_INFO("cell with bias %f %f", cell.x*cellwidth_+minx_, cell.y*cellheight_ + miny_ );
        geometry_msgs::PoseStamped next_node;
        next_node.header.seq = run_cnt_;
        next_node.header.stamp = ros::Time::now();
        next_node.header.frame_id = costmap_frame_;
        next_node.pose.position.x = cell.x*cellwidth_ + minx_ ;
        next_node.pose.position.y = cell.y*cellheight_ + miny_;

        plan.push_back(next_node);



    }

    return 1;


}
