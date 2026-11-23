module golden(
  input logic [15:0] A,
  output logic [4:0] LZ, LO, TZ, TO);
  always_comb begin
    LZ = 5'd16; LO = 5'd16; TZ = 5'd16; TO = 5'd16;
    for (int i = 15; i >= 0; i--) begin
      if (A[i] && LZ == 5'd16) LZ = 15 - i;
      if (!A[i] && LO == 5'd16) LO = 15 - i;
    end
    for (int i = 0; i <= 15; i++) begin
      if (A[i] && TZ == 5'd16) TZ = i;
      if (!A[i] && TO == 5'd16) TO = i;
    end
  end
endmodule
