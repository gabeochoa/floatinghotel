# Step 02 navigation contract

## Usage

`navigation::open(repo, reading::source(path, revision, line))` opens source. `navigation::open(repo, reading::review(scope, file))` selects a review destination. `navigation::activate(repo, Slot::Review)`, `navigation::close_source(repo)`, `navigation::step(repo, direction)`, and `navigation::return_to_review(repo)` cover the remaining two-slot operations. Source opening can carry an explicit ReviewLocation origin. Callers never clear caches or set focus themselves.

## Shape

Revision is a variant of WorkingTree, Index, RevisionQuery, and ObjectId. A source contains path and revision. A review is a variant of WorkingChanges, CommitReview, and ComparisonReview. ReviewLocation adds the selected file separately. SourceLocation adds the requested line and optional originating ReviewLocation. ReadingWorkspace stores two locations, the active slot, event history, and a navigation generation. All reads are const. Mutations are private to the navigation boundary. Active worker tasks and content stay in existing ECS runtime fields.

The navigation boundary applies state changes and runtime invalidation immediately. It raises one pending UI effect, consumed in MainContentSystem, to dismiss transient UI and return focus. Re-selecting equal state is a no-op. History records these explicit operations; frame-sampling is deleted. Renderers compare full request stamps, including repository, destination, request key, and generation, before accepting results. Resolving a historical query upgrades the stored source revision and the matching visit.

## Synthesis decision

Choose direct ECS mutation behind a small boundary. Take typed values and explicit transitions from the value candidate. Reject copying the workspace together with noncopyable worker tasks, enum-plus-optional destinations, and a generic effect-list dispatcher. Public callers gain no benefit from these layers. Retain only a one-shot UI effect because UI focus is owned by the native context. Legacy fields become const derived queries, then all writers migrate in this commit.

## Scope and tradeoffs

Step 02 retains two visible slots and existing runtime caches. Preview/keep behavior, arbitrary document collections, per-document Find, logical restoration, and revision-aware Quick Open remain in their numbered later commits. A pending revision query becomes a resolved object ID when its matching read completes. Comparison inputs remain editable form state; loaded comparison identity is typed. The review storage scope remains review persistence policy.

## Risks and verification

The largest migration risks are saved comment restoration, merge-parent selection, pending comparison results, and asynchronous source completion after navigation. Compile-time removal of mutable fields plus transition tests cover ownership; existing native navigation and baseline journeys cover actual UI dispatch. No synchronous revision resolution is permitted in click handlers.
