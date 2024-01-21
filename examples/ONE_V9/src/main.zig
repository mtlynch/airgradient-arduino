const std = @import("std");

const airgradient = @cImport({
    @cInclude("airgradient-lib.h");
});

pub fn main() !void {
    // Prints to stderr (it's a shortcut based on `std.io.getStdErr()`)
    std.debug.print("All your {s} are belong to us.\n", .{"codebase"});

    var pm2 = airgradient.PM_TO_AQI_US(5);
    std.debug.print("pm2={d}\n", .{pm2});

    // stdout is for the actual output of your application, for example if you
    // are implementing gzip, then only the compressed bytes should be sent to
    // stdout, not any debugging messages.
    const stdout_file = std.io.getStdOut().writer();
    var bw = std.io.bufferedWriter(stdout_file);
    const stdout = bw.writer();

    try stdout.print("Run `zig build test` to run the tests.\n", .{});

    try bw.flush(); // don't forget to flush!
}

test "PM to AQI test" {
    try std.testing.expectEqual(@as(c_int, 20), airgradient.PM_TO_AQI_US(@as(u8, 5)));
}
