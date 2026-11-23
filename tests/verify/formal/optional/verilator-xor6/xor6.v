// ============================================================
// 开关级模型:六管异或门
//   每条"支路" = 源 → 串联链 → Y,用三元组描述:
//     en   : 结构上是否导通
//     st   : 导通时能否给出满摆幅(strong)
//     v    : 导通时送出的电平
//   单管规则(与 Python 版独立编写):
//     N(g)   en = (g==1)   st = (v==0)    NMOS 传 0 强,传 1 弱
//     P(g)   en = (g==0)   st = (v==1)    PMOS 传 1 强,传 0 弱
//     TGL(g) en = (g==0)   st = 1         栅极 g / ~g 双相驱动
//   BREAK=1 是负对照:把 N3 的栅极错接到 GND —— 永不导通
// ============================================================
module xor6_sw #(parameter bit BREAK = 1'b0) (
  input  logic A, B,
  output logic Y, ok
);
  logic Ab;
  assign Ab = ~A;                        // 反相器:2T

  logic en1, st1, v1;                    // B  --TGL(A)-- Y
  logic en2, st2, v2;                    // A  -- P(B) -- Y
  logic en3, st3, v3;                    // Ab -- N(B) -- Y

  assign en1 = (A  == 1'b0);  assign st1 = 1'b1;         assign v1 = B;
  assign en2 = (B  == 1'b0);  assign st2 = (A == 1'b1);  assign v2 = A;
  assign en3 = BREAK ? 1'b0 : (B == 1'b1);
                              assign st3 = (Ab == 1'b0); assign v3 = Ab;

  logic any_en, any_strong, agree;
  always_comb begin
    any_en     = en1 | en2 | en3;
    any_strong = (en1 & st1) | (en2 & st2) | (en3 & st3);
    agree      = 1'b1;
    if (en1 & en2 & (v1 != v2)) agree = 1'b0;
    if (en1 & en3 & (v1 != v3)) agree = 1'b0;
    if (en2 & en3 & (v2 != v3)) agree = 1'b0;
  end

  // 良构:至少一个强驱动,且所有导通支路电平一致
  assign ok = any_en & any_strong & agree;

  always_comb begin
    if      (en1 & st1) Y = v1;
    else if (en2 & st2) Y = v2;
    else if (en3 & st3) Y = v3;
    else if (en1)       Y = v1;
    else if (en2)       Y = v2;
    else if (en3)       Y = v3;
    else                Y = 1'b0;
  end
endmodule
