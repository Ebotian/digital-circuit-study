// Behavioural reference for add4s: signed overflow iff the operands share a
// sign and the sum's sign differs.
module golden(
  input logic [3:0] A, B,
  output logic [3:0] S,
  output logic V);
  wire [4:0] R = A + B;
  assign S = R[3:0];
  assign V = (A[3] == B[3]) && (R[3] != A[3]);
endmodule
