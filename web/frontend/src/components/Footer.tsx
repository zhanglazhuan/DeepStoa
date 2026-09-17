import { Link } from 'react-router-dom'

const columns = [
  {
    title: 'Product',
    links: [
      { to: '/product', label: 'Overview' },
      { to: '/product', label: 'Features' },
      { to: '/product', label: 'Specs' },
    ],
  },
  {
    title: 'Use Cases',
    links: [
      { to: '/use-cases', label: 'Reading' },
      { to: '/use-cases', label: 'Flashcards' },
      { to: '/use-cases', label: 'Time Management' },
    ],
  },
  {
    title: 'Company',
    links: [
      { to: '/about', label: 'About' },
      { to: '/about', label: 'Contact' },
    ],
  },
  {
    title: 'Legal',
    links: [
      { to: '#', label: 'Privacy Policy' },
      { to: '#', label: 'Terms of Service' },
    ],
  },
]

export default function Footer() {
  return (
    <footer className="bg-neutral-900 px-6 py-20 text-neutral-400 lg:px-12">
      <div className="mx-auto max-w-7xl">
        <div className="grid grid-cols-2 gap-8 md:grid-cols-4">
          {columns.map((col) => (
            <div key={col.title}>
              <h4 className="mb-4 text-sm font-medium text-neutral-300">{col.title}</h4>
              <ul className="space-y-2">
                {col.links.map((link) => (
                  <li key={link.label}>
                    <Link
                      to={link.to}
                      className="text-sm transition-colors hover:text-white"
                    >
                      {link.label}
                    </Link>
                  </li>
                ))}
              </ul>
            </div>
          ))}
        </div>
        <div className="mt-16 border-t border-neutral-800 pt-8">
          <p className="text-sm">&copy; {new Date().getFullYear()} DeepStoa. All rights reserved.</p>
        </div>
      </div>
    </footer>
  )
}
