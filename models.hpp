#ifndef SOL_MODELS_HPP_INCLUDE_GUARD_
#define SOL_MODELS_HPP_INCLUDE_GUARD_

#include "typedef.h"
#include "string.hpp"

// @Note model ids must appear in the enum in the same order that they appear in 'model_file_names'
enum Model_Id {
    MODEL_ID_CUBE       = 0,
    MODEL_ID_CESIUM_MAN = 1,
};

static String g_model_file_names[] = {
    cstr_to_string("Cube.gltf"),
    cstr_to_string("CesiumMan.gltf"),
};
static String g_model_dir_names[] = {
    cstr_to_string("models/cube-static/"),
    cstr_to_string("models/cesium-man/"),
};

static const u32 g_model_file_count = sizeof(g_model_file_names) / sizeof(g_model_file_names[0]);

//
// The below is intended simulate model subsets. In a more developed app, I would want to load models as they
// correspond to particular subsets. For instance, I imagine here CesiumMan as a player model, as it is an animated
// figure with some arbitrary skin an material attributes.
//
// If one were to use only one model loading function which branched on all potential vertex and material attributes
// that a model might have, this would be an expensive and annoying to maintain function. It would be preferable
// to instead branch once outside the function in order to call the appropriate function to load the given model
// type. This requires writing more functions, but each function is faaar easier to reason about, as it is clear
// about what exactly it will be doing.
//
// Some examples: it is likely the case that we have many building models, but in terms of loading them all, we
// only need one function which can manage them all without having to dynamically branch, as a building has a
// predefined set of attributes (maybe a metallic roughness texture, normal texture, but no emissive texture).
// Now we want to load cars, but we cannot use the same function, as now we need an emissive texture for the
// headlights, plus the glass, interior and the body require different meshes, primitives etc., but again one
// function is appropriate for all cars.
//
// As I do have a number of models which all fit given subsets, I am simulating it in this little way. I think it is
// a decent implementation...???
//
enum Model_Type {
    MODEL_TYPE_INVALID  = 0,
    MODEL_TYPE_CUBE     = 1,
    MODEL_TYPE_PLAYER   = 2,
    MODEL_TYPE_BUILDING = 3,
};
Model_Type get_model_type(Model_Id id) {
    switch(id) {
    case MODEL_ID_CUBE:
        return MODEL_TYPE_CUBE;
    case MODEL_ID_CESIUM_MAN:
        return MODEL_TYPE_PLAYER;
    }
}

#endif
