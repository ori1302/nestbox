/// A message the Quick Test menu sends to make the pet react at once.
struct QuickTest {
    private static let from = "menubar"

    let title: String
    let message: PetMessage

    static let all = [
        QuickTest(title: "Done", message: PetMessage(from: from, state: "done")),
        QuickTest(title: "Needs you",
                  message: PetMessage(from: from, state: "asking", say: "Quick test: ok?", seconds: 15)),
        QuickTest(title: "Failed", message: PetMessage(from: from, state: "failed")),
        QuickTest(title: "Say hello", message: PetMessage(from: from, doing: "wave", say: "hello!", sign: "heart")),
    ]
}
