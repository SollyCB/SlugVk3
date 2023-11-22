#include "glfw.hpp"
#include "camera.hpp"

static Glfw *s_Glfw;
Glfw* get_glfw_instance() {
    return s_Glfw;
}

void mouse_callback(GLFWwindow *window, double x, double y) {
    Camera *cam = get_camera_instance();
    if (!cam->have_turned) {
        cam->last_x = x;
        cam->last_y = y;
        cam->have_turned = true;
    }
    float dx = cam->last_x - x;
    float dy = cam->last_y - y;
    cam->last_x = x;
    cam->last_y = y;
    camera_turn(cam, dx, dy);
}

void init_glfw() {
    glfwInit();

    // no opengl context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    glfwSetErrorCallback(error_callback_glfw);

    s_Glfw = (Glfw*)malloc_h(sizeof(Glfw), 8);

    Glfw *glfw = get_glfw_instance();
    glfw->window = glfwCreateWindow(640, 480, "GLFW Window", NULL, NULL);
    assert(glfw->window && "GLFW Fail To Create Window");

    // @Todo window resizing

    glfwSetInputMode(glfw->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(glfw->window, mouse_callback);
}

void kill_glfw(Glfw *glfw) {
    glfwDestroyWindow(glfw->window);
    glfwTerminate();
    free_h(glfw);
}

enum InputValues {
    INPUT_CLOSE_WINDOW  = GLFW_KEY_Q,
    INPUT_MOVE_FORWARD  = GLFW_KEY_W,
    INPUT_MOVE_BACKWARD = GLFW_KEY_S,
    INPUT_MOVE_RIGHT    = GLFW_KEY_D,
    INPUT_MOVE_LEFT     = GLFW_KEY_A,
    INPUT_JUMP          = GLFW_KEY_SPACE,
};

void poll_and_get_input(Glfw *glfw) {
    glfwPollEvents();

    int right = 0;
    int forward = 0;
    if (glfwGetKey(glfw->window, (int)INPUT_CLOSE_WINDOW) == GLFW_PRESS) {
        glfwSetWindowShouldClose(glfw->window, GLFW_TRUE);
        println("\nINPUT_CLOSE_WINDOW Key Pressed...");
    }
    if (glfwGetKey(glfw->window, (int)INPUT_MOVE_FORWARD) == GLFW_PRESS)
        forward = 1;
    if (glfwGetKey(glfw->window, (int)INPUT_MOVE_BACKWARD) == GLFW_PRESS)
        forward = -1;
    if (glfwGetKey(glfw->window, (int)INPUT_MOVE_RIGHT) == GLFW_PRESS)
        right = 1;
    if (glfwGetKey(glfw->window, (int)INPUT_MOVE_LEFT) == GLFW_PRESS)
        right = -1;

    camera_move(get_camera_instance(), forward, right);
}

