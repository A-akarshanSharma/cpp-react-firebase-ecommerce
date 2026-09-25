import { Button } from './UI'
export default function LoadMore({ resource }) {
  const { data, nextCursor, loading, error, moreError, loadingMore, loadMore } = resource
  if (loading || error) return null
  return (
    <div className="section">
      <p className="muted" role="status">
        {data.length} records loaded{nextCursor ? ' · More available' : ''}
      </p>
      {moreError && (
        <p className="form-error" role="alert">
          {moreError}
        </p>
      )}
      {nextCursor && (
        <Button variant="secondary" disabled={loadingMore} onClick={loadMore}>
          {loadingMore ? 'Loading…' : moreError ? 'Retry loading more' : 'Load more'}
        </Button>
      )}
    </div>
  )
}
