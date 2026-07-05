#version 100
/* GLSL ES 1.0 — required by the Android raylib codepath; desktop GL
 * accepts it too. Single shader, axis chosen via the `direction` uniform
 * so a horizontal pass uses (1,0) and a vertical pass uses (0,1).
 *
 * 9-tap discrete Gaussian, sigma ≈ 1.8. Soft enough to obscure scene
 * detail at the menu's distance, sharp enough to keep two passes from
 * smearing into a flat colour. Weights sum to 1.0. */
precision mediump float;
varying vec2 fragTexCoord;
varying vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 texelSize;
uniform vec2 direction;
void main() {
    vec2 offset = texelSize * direction;
    vec3 sum = vec3(0.0);
    sum += texture2D(texture0, fragTexCoord - 4.0 * offset).rgb * 0.0162162162;
    sum += texture2D(texture0, fragTexCoord - 3.0 * offset).rgb * 0.0540540541;
    sum += texture2D(texture0, fragTexCoord - 2.0 * offset).rgb * 0.1216216216;
    sum += texture2D(texture0, fragTexCoord - 1.0 * offset).rgb * 0.1945945946;
    sum += texture2D(texture0, fragTexCoord                ).rgb * 0.2270270270;
    sum += texture2D(texture0, fragTexCoord + 1.0 * offset).rgb * 0.1945945946;
    sum += texture2D(texture0, fragTexCoord + 2.0 * offset).rgb * 0.1216216216;
    sum += texture2D(texture0, fragTexCoord + 3.0 * offset).rgb * 0.0540540541;
    sum += texture2D(texture0, fragTexCoord + 4.0 * offset).rgb * 0.0162162162;
    gl_FragColor = vec4(sum, 1.0) * fragColor;
}
