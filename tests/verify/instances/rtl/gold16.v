module golden(
  input logic A15, A14, A13, A12, A11, A10, A9, A8,
  input logic A7, A6, A5, A4, A3, A2, A1, A0,
  output logic Y3, Y2, Y1, Y0);
  logic [3:0] y;
  always_comb begin
    y = 4'd0;
    if (A0) y = 4'd0;  if (A1) y = 4'd1;  if (A2) y = 4'd2;  if (A3) y = 4'd3;
    if (A4) y = 4'd4;  if (A5) y = 4'd5;  if (A6) y = 4'd6;  if (A7) y = 4'd7;
    if (A8) y = 4'd8;  if (A9) y = 4'd9;  if (A10) y = 4'd10; if (A11) y = 4'd11;
    if (A12) y = 4'd12; if (A13) y = 4'd13; if (A14) y = 4'd14; if (A15) y = 4'd15;
  end
  assign Y3 = y[3]; assign Y2 = y[2]; assign Y1 = y[1]; assign Y0 = y[0];
endmodule
