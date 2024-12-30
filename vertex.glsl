uniform vec2 u_ScreenSize;
attribute vec4 a_Position;

void main() 
{
    vec2 position = a_Position.xy;
    vec2 ndc = (position / u_ScreenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
}