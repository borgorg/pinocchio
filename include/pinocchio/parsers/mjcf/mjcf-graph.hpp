//
// Copyright (c) 2015-2024 CNRS INRIA
//

#ifndef __pinocchio_parsers_mjcf_graph_hpp__
#define __pinocchio_parsers_mjcf_graph_hpp__

#include "pinocchio/parsers/urdf.hpp"
#include "pinocchio/multibody/model.hpp"
#include "pinocchio/multibody/joint/joints.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/foreach.hpp>
#include <boost/math/constants/constants.hpp>
#include <boost/filesystem.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/lexical_cast.hpp>

#include <sstream>
#include <limits>
#include <iostream>



namespace pinocchio
{
    namespace mjcf
    {
        namespace details 
        {
            struct MjcfGraph;
            struct MjcfJoint;
            struct MjcfGeom;

            /// @brief Informations that are stocked in the XML tag compile.
            /// 
            struct MjcfCompiler
            {
                public:
                    // Global attribute to use limit that are in the model or not
                    bool autolimits = true;
                    
                    // Attribute to keep or not the full path of files specified in the model
                    bool strippath = false;
                    // Directory where all the meshes are (can be relative or absolute)
                    std::string meshdir; 
                    // Directory where all the textures are (can be relative or absolute)
                    std::string texturedir;

                    // Value for angle conversion (Mujoco default - degrees)
                    double angle_converter = boost::math::constants::pi<double>() / 180.0;
                    // Euler Axis to use to convert angles representation to quaternion
                    Eigen::Matrix3d mapEulerAngles;

                    // Value to crop the mass (if mass < boundMass, mass = boundMass)
                    double boundMass = 0;
                    // Value to crop the diagonal of the inertia matrix (if mass < boundMass, mass = boundMass)
                    double boundInertia = 0;

                    // True, false or auto - auto = indeterminate
                    boost::logic::tribool inertiafromgeom = boost::logic::indeterminate;
                    
                    /// @brief Convert the angle in radian if model was declared to use degree
                    /// @param angle_ angle to convert
                    /// @return converted angle
                    double convertAngle(const double &angle_) const;

                    /// @brief Convert the euler angles according to the convention declared in the compile tag. 
                    /// @param angles Euler angles
                    /// @return Quaternion representation of the euler angles
                    Eigen::Matrix3d convertEuler(const Eigen::Vector3d &angles) const;
            };

            /// @brief Structure to stock all default classes information
            struct MjcfClass
            {
                public:
                    typedef boost::property_tree::ptree ptree;

                    // name of the default class
                    std::string className;
                    // Ptree associated with the class name
                    ptree classElement;
            };

            /// @brief All Bodies informations extracted from mjcf model
            struct MjcfBody
            {
                public:
                    // Name of the body
                    std::string bodyName;
                    // Name of the parent 
                    std::string bodyParent;
                    // Name of the default class used by this body (optional)
                    std::string bodyClassName;
                    // Special default class, that is common to all bodies and children if not specified otherwise
                    std::string childClass;

                    // Position of the body wrt to the previous body
                    SE3 bodyPlacement = SE3::Identity();
                    // Body inertia
                    Inertia bodyInertia = Inertia::Identity();

                    // Vector of joints associated with the body
                    std::vector<MjcfJoint> jointChildren;
                    // Vector of geometries associated with the body
                    std::vector<MjcfGeom> geomChildren;
            };

            /// @brief All joint limits
            struct RangeJoint
            {
                // Max effort
                Eigen::VectorXd maxEffort;
                // Max velocity
                Eigen::VectorXd maxVel;
                // Max position
                Eigen::VectorXd maxConfig;
                // Min position
                Eigen::VectorXd minConfig;

                // Join Stiffness
                Eigen::VectorXd springStiffness;
                //  joint position or angle in which the joint spring (if any) achieves equilibrium
                Eigen::VectorXd springReference;

                // friction applied in this joint
                Eigen::VectorXd friction;
                // Damping applied by this joint. 
                Eigen::VectorXd damping;

                // Armature inertia created by this joint
                double armature = 0.;
                // Dry friction.
                double frictionLoss = 0.;

                RangeJoint() = default;
                explicit RangeJoint(double v)
                {
                    const double infty = std::numeric_limits<double>::infinity();
                    maxVel = Eigen::VectorXd::Constant(1, infty);
                    maxEffort = Eigen::VectorXd::Constant(1, infty);
                    minConfig = Eigen::VectorXd::Constant(1, - infty);
                    maxConfig = Eigen::VectorXd::Constant(1, infty);
                    springStiffness = Eigen::VectorXd::Constant(1, v);
                    springReference = Eigen::VectorXd::Constant(1, v);;
                    friction = Eigen::VectorXd::Constant(1,0.);
                    damping = Eigen::VectorXd::Constant(1,0.);  
                }

                /// @brief Set dimension to the limits to match the joint nq and nv.
                /// @tparam Nq joint configuration
                /// @tparam Nv joint velocity
                /// @return Range with new dimension
                template<int Nq, int Nv>
                RangeJoint setDimension() const;

                /// @brief Concatenate 2 rangeJoint
                /// @tparam Nq old_range, joint configuration 
                /// @tparam Nv old_range, joint velocity
                /// @param range to concatenate with
                /// @return Concatenated range.
                template<int Nq, int Nv>
                RangeJoint concatenate(const RangeJoint &range) const;
            };

            /// @brief All joint information parsed from the mjcf model
            struct MjcfJoint
            {
                public:
                    typedef boost::property_tree::ptree ptree;

                    // Name of the joint
                    std::string jointName = "free";
                    // Placement of the joint wrt to its body - default Identity
                    SE3 jointPlacement = SE3::Identity();

                    // axis of the joint - default "0 0 1"
                    Eigen::Vector3d axis = Eigen::Vector3d::UnitZ();
                    // Limits that applie to this joint
                    RangeJoint range{1};

                    // type of the joint (hinge, ball, slide, free) - default "hinge"
                    std::string jointType = "hinge";

                    /// @param el ptree joint node 
                    /// @param currentBody body to which the joint belongs to 
                    /// @param currentGraph current Mjcf graph (needed to get compiler information)
                    void fill(const ptree &el, const MjcfBody &currentBody, const MjcfGraph &currentGraph);
                    
                    /// @brief Go through a joint node (default class or not) and parse info into the structure
                    /// @param el ptree joint node
                    /// @param use_limits whether to parse the limits or not 
                    void goThroughElement(const ptree &el, bool use_limits);
            };
            /// @brief All informations related to a mesh are stored here
            struct MjcfMesh
            {
                // Scale of the mesh
                Eigen::Vector3d scale = Eigen::Vector3d::Constant(1);
                // Path to the mesh file
                std::string filePath;
            };

            /// @brief All informations related to a texture are stored here
            struct MjcfTexture
            {
                // [2d, cube, skybox], “cube”
                std::string textType = "cube";
                // Path to the texture file
                std::string filePath;
                // Size of the grid if needed
                Eigen::Vector2d gridsize = Eigen::Vector2d::Constant(1);
            };

            /// @brief All informations related to material are stored here
            struct MjcfMaterial
            {
                typedef boost::property_tree::ptree ptree;
                // Color of the material
                Eigen::Vector4d rgba = Eigen::Vector4d::Constant(1);

                float reflectance = 0;

                float shininess = 0.5;

                float specular = 0.5;

                float emission = 0;
                // name of the texture to apply on the material
                std::string texture;

                /// @brief Go through a ptree node to look for material tag related
                /// @param el ptree material node
                void goThroughElement(const ptree &el);
            };

            struct MjcfGeom
            {
                public:
                    typedef boost::property_tree::ptree ptree;

                    // Kind of possible geometry 
                    enum TYPE {VISUAL, COLLISION, BOTH};
                    // name of the geometry object
                    std::string geomName;

                    // [plane, hfield, sphere, capsule, ellipsoid, cylinder, box, mesh, sdf], “sphere”
                    std::string geomType = "sphere";

                    // Kind of the geometry object
                    TYPE geomKind = BOTH;

                    // Contact filtering and dynamic pair (used here to determine geometry kind)
                    int contype = 1;
                    int conaffinity = 1;
                    // Geometry group (used to determine geometry kind)
                    int group = 0;

                    // String that hold size parameter 
                    std::string sizeS;
                    // Optional in case fromto tag is used
                    boost::optional<std::string> fromtoS;
                    // Size parameter
                    Eigen::VectorXd size;

                    // Color of the geometry
                    Eigen::Vector4d rgba = Eigen::Vector4d::Constant(1);
                    
                    // Name of the material applied on the geometry
                    std::string materialName;
                    // Name of the mesh (optional)
                    std::string meshName;

                    // Density for computing the mass
                    double density = 1000;
                    // If mass is only on the outer layer of the geometry
                    bool shellinertia = false;

                    // Geometry Placement in parent body. Center of the frame of geometry is the center of mass.
                    SE3 geomPlacement = SE3::Identity();
                    // Inertia of the geometry obj
                    Inertia geomInertia = Inertia::Identity();
                    // optional mass (if not defined, will use density)
                    boost::optional<double> massGeom;

                    /// @brief Find the geometry kind 
                    void findKind();

                    /// @brief Compute Geometry size based on sizeS and fromtoS
                    void computeSize();

                    /// @brief Compute geometry inertia
                    void computeInertia();

                    /// @brief Fill Geometry element with info from ptree nodes
                    void fill(const ptree &el, const MjcfBody &currentBody, const MjcfGraph &currentGraph);

                    /// @bried Go through a geom ptree node, to gather informations
                    void goThroughElement(const ptree &el, const MjcfGraph &currentGraph);
            };

            /// @brief The graph which contains all information taken from the mjcf file
            struct MjcfGraph
            {
                public:
                    typedef boost::property_tree::ptree ptree;
                    typedef std::vector<std::string> VectorOfStrings;
                    typedef std::unordered_map<std::string, MjcfBody> BodyMap_t;
                    typedef std::unordered_map<std::string, MjcfClass> ClassMap_t;
                    typedef std::unordered_map<std::string, MjcfMaterial> MaterialMap_t;
                    typedef std::unordered_map<std::string, MjcfMesh> MeshMap_t;
                    typedef std::unordered_map<std::string, MjcfTexture> TextureMap_t;

                    // Compiler Info needed to properly parse the rest of file
                    MjcfCompiler compilerInfo;
                    // Map of default classes 
                    ClassMap_t mapOfClasses;
                    // Map of bodies
                    BodyMap_t mapOfBodies;
                    // Map of Materials
                    MaterialMap_t mapOfMaterials;
                    // Map of Meshes 
                    MeshMap_t mapOfMeshes;
                    //Map of textures
                    TextureMap_t mapOfTextures;

                    // property tree where xml file is stored
                    ptree pt;

                    // Ordered list of bodies
                    VectorOfStrings bodiesList;

                    // Name of the model
                    std::string modelName;
                    std::string modelPath;

                    // Urdf Visitor to add joint and body
                    typedef pinocchio::urdf::details::UrdfVisitor<double, 0, ::pinocchio::JointCollectionDefaultTpl > UrdfVisitor;
                    UrdfVisitor& urdfVisitor;

                    /// @brief graph constructor
                    /// @param urdfVisitor 
                    MjcfGraph(UrdfVisitor& urdfVisitor, const std::string &modelPath):
                    modelPath(modelPath),
                    urdfVisitor(urdfVisitor)
                    {}

                    /// @brief Convert pose of an mjcf element into SE3
                    /// @param el ptree element with all the pose element
                    /// @return pose in SE3
                    SE3 convertPosition(const ptree &el) const;

                    /// @brief Convert Inertia of an mjcf element into Inertia model of pinocchio
                    /// @param el ptree element with all the inertial information
                    /// @return Inertia element in pinocchio
                    Inertia convertInertiaFromMjcf(const ptree &el) const;

                    /// @brief Go through the default part of the file and get all the class name. Fill the mapOfDefault for later use.
                    /// @param el ptree element. Root of the default
                    void parseDefault(ptree &el, const ptree &parent);

                    /// @brief Go through the main body of the mjcf file "worldbody" to get all the info ready to create the model.
                    /// @param el root of the tree
                    /// @param parentName name of the parentBody in the robot tree
                    void parseJointAndBody(const ptree &el, const boost::optional<std::string> &childClass, const std::string &parentName="");

                    /// @brief Parse all the info from the compile node into compilerInfo
                    /// @param el ptree compile node
                    void parseCompiler(const ptree &el);

                    /// @brief Parse all the info from a texture node
                    /// @param el ptree texture node
                    void parseTexture(const ptree &el);

                    /// @brief Parse all the info from a material node
                    /// @param el ptree material node
                    void parseMaterial(const ptree &el);

                    /// @brief Parse all the info from a mesh node
                    /// @param el ptree mesh node
                    void parseMesh(const ptree &el);

                    /// @brief Parse all the info from the meta tag asset (mesh, material, texture)
                    /// @param el ptree texture node
                    void parseAsset(const ptree &el);

                    /// @brief parse the mjcf file into a graph
                    void parseGraph();

                    /// @brief parse the mjcf file into a graph
                    /// @param xmlStr xml file name 
                    void parseGraphFromXML(const std::string &xmlStr);

                    /// @brief Create a joint to add to the joint composite if needed
                    /// @tparam TypeX joint with axis X
                    /// @tparam TypeY joint with axis Y
                    /// @tparam TypeZ joint with axis Z
                    /// @tparam TypeUnaligned joint with axis unaligned
                    /// @param axis axis of the joint
                    /// @return one of the joint with the right axis
                    template <typename TypeX, typename TypeY, typename TypeZ, typename TypeUnaligned>
                    JointModel createJoint(const Eigen::Vector3d& axis);

                    /// @brief Add a joint to the model. only needed when a body has a solo joint child
                    /// @param jointInfo The joint to add to the tree
                    /// @param currentBody The body associated with the joint
                    void addSoloJoint(const MjcfJoint &jointInfo, const MjcfBody &currentBody);

                    /// @brief Use all the infos that were parsed from the xml file to add a body and joint to the model
                    /// @param nameOfBody Name of the body to add
                    void fillModel(const std::string &nameOfBody);

                    /// @brief Fill the pinocchio model with all the infos from the graph
                    void parseRootTree();   
                    
                    /// @brief Fill geometry model with all the info taken from the mjcf model file
                    /// @param type Type of geometry to parse (COLLISION or VISUAL)
                    /// @param geomModel geometry model to fill
                    /// @param meshLoader mesh loader from hpp::fcl
                    void parseGeomTree(const GeometryType& type, GeometryModel& geomModel, ::hpp::fcl::MeshLoaderPtr& meshLoader);
            };
            namespace internal
            {
                inline std::istringstream getConfiguredStringStream(const std::string& str) 
                {
                    std::istringstream posStream(str);
                    posStream.exceptions(std::ios::failbit);
                    return posStream;
                }
            
                template<int N>
                inline Eigen::Matrix<double, N, 1> getVectorFromStream(const std::string &str)
                {   
                    std::istringstream stream = getConfiguredStringStream(str);
                    Eigen::Matrix<double, N, 1> vector;
                    for(int i = 0; i < N; i++)
                        stream >> vector(i);
                    
                    return vector;
                }
            } // internal
        } // details
    } //mjcf
} //pinocchio

#endif // __pinocchio_parsers_mjcf_graph_hpp__
