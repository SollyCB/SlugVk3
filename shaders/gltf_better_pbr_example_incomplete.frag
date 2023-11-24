// @Note This is example is incomplete: It does not show implementations for G() and D(),
// but that is fine, I will add them later (they are gltf2.0 spec, appendix B.3.3)

const black = 0

c_diff = lerp(baseColor.rgb, black, metallic)
f0 = lerp(0.04, baseColor.rgb, metallic)
α = roughness^2

F = f0 + (1 - f0) * (1 - abs(VdotH))^5

f_diffuse = (1 - F) * (1 / π) * c_diff
f_specular = F * D(α) * G(α) / (4 * abs(VdotN) * abs(LdotN))

material = f_diffuse + f_specular
