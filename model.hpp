/*
------------------------------------------------------------------------------------------------------------------------

   ** The below explanation is a little incoherent at points, but the main point I think is plenty clear - I just **
   ** quickly wrote it out. @Todo improve the explanation **

------------------------------------------------------------------------------------------------------------------------

   This file is really intended to simulate model subsets as a system for model loading. In a more developed
   app, I would want to load models as they correspond to particular subsets. For instance, I imagine here
   CesiumMan as a player model, as it is an animated figure with some arbitrary skin an material attributes.

   If one were to use only one model loading function which branched on all potential vertex and material
   attributes that a model might have, this would be an expensive and annoying to maintain function. It would
   be preferable to instead branch once outside the function in order to call the appropriate function to load
   the given model type. This requires writing more functions, but each function is faaar easier to reason
   about, as it is clear about what exactly it will be doing.

   Some examples: it is likely the case that we have many building models, but in terms of loading them all,
   we only need one function which can manage them all without having to dynamically branch, as a building has
   a predefined set of attributes (maybe a metallic roughness texture, normal texture, but no emissive
   texture). Now we want to load cars, but we cannot use the same function, as now we need an emissive texture
   for the headlights, plus the glass, interior and the body require different meshes, primitives etc., but
   again one function is appropriate for all cars.

   As I do have a number of models which all fit given subsets, I am simulating it in this little way. I think
   it is a decent implementation...???

                                       ******** @Todo @Note *********
   I understand that for a larger app, defining the models in this way where the programmer has to order
   the models and define what type they are a part of is unsustainable, so later I will come to writing some
   custom file format or something which makes it easier for a human to enumerate models, and then this file
   can be generated  automatically.
*/
#ifndef SOL_MODEL_HPP_INCLUDE_GUARD_
#define SOL_MODEL_HPP_INCLUDE_GUARD_

#include "typedef.h"
#include "string.hpp"

// @Note model ids must appear in the enum in the same order that they appear in 'model_file_names'
enum Model_Id {
    MODEL_ID_CUBE       = 0,
    MODEL_ID_CESIUM_MAN = 1,
};
enum Model_Type { // These are mostly just example types for now - Sol 6 Dec 2023
    MODEL_TYPE_INVALID  = 0,
    MODEL_TYPE_CUBE     = 1,
    MODEL_TYPE_PLAYER   = 2,
    MODEL_TYPE_BUILDING = 3,
};

struct Model_Identifier {
    Model_Id   id;
    Model_Type type;
};

// @Note This identifier array must be ordered by model id
static Model_Identifier g_model_identifiers[] = {
    {MODEL_ID_CUBE,       MODEL_TYPE_CUBE},
    {MODEL_ID_CESIUM_MAN, MODEL_TYPE_PLAYER},
};

static String g_model_file_names[] = {
    cstr_to_string("Cube.gltf"),
    cstr_to_string("CesiumMan.gltf"),
};
static String g_model_dir_names[] = {
    cstr_to_string("models/cube-static/"),
    cstr_to_string("models/cesium-man/"),
};

static const u32 g_model_count = sizeof(g_model_file_names) / sizeof(g_model_file_names[0]);

#endif // include guard
