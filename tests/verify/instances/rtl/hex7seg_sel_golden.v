module golden(input logic D3,D2,D1,D0,SEL,
              output logic a,b,c,d,e,f,g);
  logic [3:0] dv = {D3,D2,D1,D0};
  logic [6:0] s;
  always_comb begin
    case (dv)
      4'h0: s = 7'b1111110;
      4'h1: s = 7'b0110000;
      4'h2: s = 7'b1101101;
      4'h3: s = 7'b1111001;
      4'h4: s = 7'b0110011;
      4'h5: s = 7'b1011011;
      4'h6: s = 7'b1011111;
      4'h7: s = 7'b1110000;
      4'h8: s = 7'b1111111;
      4'h9: s = 7'b1111011;
      4'hA: s = 7'b1110111;
      4'hB: s = 7'b0011111;
      4'hC: s = 7'b1001110;
      4'hD: s = 7'b0111101;
      4'hE: s = 7'b1001111;
      default: s = 7'b1000111;
    endcase
    if (SEL && (dv > 4'd9)) s = 7'b0;
  end
  assign {a,b,c,d,e,f,g} = s;
endmodule
