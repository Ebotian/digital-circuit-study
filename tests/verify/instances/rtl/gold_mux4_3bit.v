module golden(
  input logic [2:0] D0, D1, D2, D3,
  input logic [1:0] S,
  output logic [2:0] Y);
  always_comb begin
    case (S)
      2'd0: Y = D0;
      2'd1: Y = D1;
      2'd2: Y = D2;
      2'd3: Y = D3;
    endcase
  end
endmodule
