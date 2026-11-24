module golden(
  input logic [3:0] A, B,
  output logic LED,
  output logic SA_a, SA_b, SA_c, SA_d, SA_e, SA_f, SA_g,
  output logic SB_a, SB_b, SB_c, SB_d, SB_e, SB_f, SB_g,
  output logic SS_a, SS_b, SS_c, SS_d, SS_e, SS_f, SS_g);
  wire [4:0] R = A + B;
  assign LED = R[4];
  hex_ref ha(.D(A), .a(SA_a), .b(SA_b), .c(SA_c), .d(SA_d),
             .e(SA_e), .f(SA_f), .g(SA_g));
  hex_ref hb(.D(B), .a(SB_a), .b(SB_b), .c(SB_c), .d(SB_d),
             .e(SB_e), .f(SB_f), .g(SB_g));
  hex_ref hs(.D(R[3:0]), .a(SS_a), .b(SS_b), .c(SS_c), .d(SS_d),
             .e(SS_e), .f(SS_f), .g(SS_g));
endmodule

module hex_ref(input logic [3:0] D,
               output logic a, b, c, d, e, f, g);
  always_comb begin
    case (D)
      4'h0: {a,b,c,d,e,f,g} = 7'b1111110;
      4'h1: {a,b,c,d,e,f,g} = 7'b0110000;
      4'h2: {a,b,c,d,e,f,g} = 7'b1101101;
      4'h3: {a,b,c,d,e,f,g} = 7'b1111001;
      4'h4: {a,b,c,d,e,f,g} = 7'b0110011;
      4'h5: {a,b,c,d,e,f,g} = 7'b1011011;
      4'h6: {a,b,c,d,e,f,g} = 7'b1011111;
      4'h7: {a,b,c,d,e,f,g} = 7'b1110000;
      4'h8: {a,b,c,d,e,f,g} = 7'b1111111;
      4'h9: {a,b,c,d,e,f,g} = 7'b1111011;
      4'hA: {a,b,c,d,e,f,g} = 7'b1110111;
      4'hB: {a,b,c,d,e,f,g} = 7'b0011111;
      4'hC: {a,b,c,d,e,f,g} = 7'b1001110;
      4'hD: {a,b,c,d,e,f,g} = 7'b0111101;
      4'hE: {a,b,c,d,e,f,g} = 7'b1001111;
      default: {a,b,c,d,e,f,g} = 7'b1000111;
    endcase
  end
endmodule

